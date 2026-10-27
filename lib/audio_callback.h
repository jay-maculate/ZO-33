// Copyright 2023-2025 Zack Scholl, GPLv3.0
#include "hardware/sync.h"
#include "audio_profile.h"
#include "usb_audio.h"
#include "usb_record.h"
#include "display.h"
#include "audio_restart.h"
#if AUDIO_RESTART_WAKE_ENABLED
#include "audio_restart_queue.h"
// Owned only by the audio worker; the clock core communicates through the
// restart token and FIFO, never these fields.
static struct {
  bool active;
  bool published;
  unsigned replaced_silence;
} audio_restart_wake;
static void audio_submit_buffer(audio_buffer_t *buffer, bool restart_block) {
  if (audio_restart_wake.active || restart_block) {
    unsigned removed = audio_restart_publish_buffer(ap, buffer);
    if (audio_restart_wake.active) {
      audio_restart_wake.published = true;
      audio_restart_wake.replaced_silence += removed;
    }
  } else {
    give_audio_buffer(ap, buffer);
  }
}
#else
#define audio_submit_buffer(buffer, restart_block) give_audio_buffer(ap, buffer)
#endif
// previous output per channel (persistent state)
#ifdef INCLUDE_EZEPTOCORE
#define BASS_LP_A 64917  // alpha ≈ 0.9908
#define BASS_LP_B 619    // 1 - alpha
static int32_t smear_buf[32][2];
static uint8_t smear_pos = 0;
// auto-oscillation state
static uint8_t smear_amt = 1;
static int8_t smear_dir = 1;
static uint32_t smear_acc = 0;
static int32_t bass_lp[2] = {0, 0};
#define MICRO_DELAY_MAX 64  // ~1.45 ms @ 44.1kHz

static int32_t tremble_val[2] = {Q16_16_1, Q16_16_1};
static int32_t tremble_vel[2] = {0, 0};

#endif
uint8_t cpu_utilizations[64];
uint8_t cpu_utilizations_i = 0;
uint32_t reduce_cpu_usage = 0;
uint32_t cpu_usage_flag_total = 0;
uint8_t cpu_usage_flag = 0;
uint16_t cpu_flag_counter = 0;
const uint8_t cpu_usage_flag_limit = 60;
const uint8_t cpu_usage_limit_threshold = 150;

bool audio_was_muted = false;
bool do_open_file_ready = false;
bool muted_because_of_sel_variation = false;
bool first_loop_ever = true;
int32_t reverb_fade = 0;
int32_t amiga_previous_value[2];
bool reverb_activated = false;
bool mute_soft_activated = false;

#include "audio_silent_switch.h"

#define INTERPOLATE_VALUE 512
#include "audio_resample.h"
int16_t newArray[SAMPLES_PER_BUFFER];
// Core 1's stack is reserved for control flow and short-lived source buffers.
// This always-present 3.5 KiB render workspace is static so normal stereo
// playback fits within the RP2040 scratch-bank stack.
static int32_t audio_render_samples[SAMPLES_PER_BUFFER * 2];

void __not_in_flash_func(array_resample_linear441)(const int16_t *arr,
                                                   uint32_t arr_size,
                                                   uint32_t stride) {
  AP_MEASURE(AP_RESAMPLE,audio_resample_linear(newArray, arr, arr_size, SAMPLES_PER_BUFFER, stride));
}


#ifdef DEBUG_AUDIO_WITH_SINE_WAVE
uint32_t sine_wave_counter = 0;
#endif

static void __not_in_flash_func(sample_slot_filter_voice)(SampleSlotVoice *v,
                                                           int32_t *samples,
                                     uint32_t frames, int8_t cutoff,
                                     uint8_t resonance) {
  if (cutoff == 0 || frames == 0) return;
  if (v->applied_filter != cutoff || v->applied_filter_q != resonance) return;
  if (v->filter_reset) {
    v->filter_l.x1_f = v->filter_l.x2_f = v->filter_l.y1_f = v->filter_l.y2_f =
        0;
    v->filter_r.x1_f = v->filter_r.x2_f = v->filter_r.y1_f = v->filter_r.y2_f =
        0;
    v->filter_reset = false;
  }
  ResonantFilter_process_block(&v->filter_l, samples, frames, 0, 0);
  ResonantFilter_process_block(&v->filter_r, samples, frames, 1, 0);
}

static uint8_t slot_pull_sec[512];
static uint16_t slot_pull_pos;
static uint16_t slot_pull_len;
static uint8_t slot_pull_voice = 0xff;

static void slot_pull_reset(uint8_t voice) {
  if (slot_pull_voice == voice) return;
  slot_pull_voice = voice;
  slot_pull_pos = 0;
  slot_pull_len = 0;
}

static uint32_t sample_slot_pull(SampleSlotVoice *v, void *dst, uint32_t n) {
  uint8_t *out = dst;
  uint32_t got_n = 0;
  while (n) {
    if (slot_pull_pos >= slot_pull_len) {
      uint32_t aligned = v->file_at & ~511u;
      uint32_t skip = v->file_at - aligned;
      if (f_tell(&v->fil) != aligned && f_lseek(&v->fil, aligned) != FR_OK) return got_n;
      UINT got = 0;
      if (f_read(&v->fil, slot_pull_sec, 512, &got) != FR_OK || got <= skip) return got_n;
      slot_pull_len = (uint16_t)got;
      slot_pull_pos = (uint16_t)skip;
    }
    uint32_t have = (uint32_t)slot_pull_len - slot_pull_pos;
    uint32_t take = n < have ? n : have;
    memcpy(out, slot_pull_sec + slot_pull_pos, take);
    slot_pull_pos = (uint16_t)(slot_pull_pos + take);
    v->file_at += take;
    out += take;
    got_n += take;
    n -= take;
  }
  return got_n;
}

static void sample_slot_note_short_read(SampleSlotVoice *v, UINT got,
                                         uint32_t frame_bytes) {
  UINT extra = frame_bytes ? got % frame_bytes : 0;
  if (extra && v->reserve && v->file_at >= extra) {
    v->file_at -= extra;
    if (slot_pull_pos >= extra) slot_pull_pos = (uint16_t)(slot_pull_pos - extra);
    else slot_pull_len = 0;
  } else if (extra) {
    FSIZE_t pos = f_tell(&v->fil);
    if (pos >= extra) f_lseek(&v->fil, pos - extra);
  }
  if (v->short_read) {
    f_close(&v->fil);
    v->open = false;
    v->short_read = false;
    v->seek = false;
    return;
  }
  v->short_read = true;
  v->seek = true;
  v->primed = false;
  v->have_ahead = false;
}

static int16_t *pref_data(SampleSlotVoice *v) {
  uint8_t voice = (uint8_t)(v - sample_slot_voice);
  return sample_slot_pref_mem + (uint32_t)voice * SAMPLE_SLOT_PREF_FRAMES * 2u;
}

static uint16_t pref_count(SampleSlotVoice *v) {
  uint16_t w = v->pref_w;
  __dmb();
  uint16_t r = v->pref_r;
  return (uint16_t)((w + SAMPLE_SLOT_PREF_FRAMES - r) % SAMPLE_SLOT_PREF_FRAMES);
}

static void pref_clear(SampleSlotVoice *v) {
  __dmb();
  v->pref_w = v->pref_r;
  v->pref_eof = false;
}

static void pref_push(SampleSlotVoice *v, const int16_t *stereo, uint32_t frames) {
  uint16_t r = v->pref_r;
  __dmb();
  uint16_t w = v->pref_w;
  uint16_t fill = (uint16_t)((w + SAMPLE_SLOT_PREF_FRAMES - r) % SAMPLE_SLOT_PREF_FRAMES);
  uint32_t space = (SAMPLE_SLOT_PREF_FRAMES - 1u) - fill;
  if (frames > space) frames = space;
  int16_t *slot = pref_data(v);
  uint16_t at = w;
  for (uint32_t i = 0; i < frames; i++) {
    slot[at * 2] = stereo[i * 2];
    slot[at * 2 + 1] = stereo[i * 2 + 1];
    if (++at == SAMPLE_SLOT_PREF_FRAMES) at = 0;
  }
  __dmb();
  v->pref_w = at;
}

static uint32_t pref_take(SampleSlotVoice *v, int16_t *dst, uint32_t max) {
  uint16_t w = v->pref_w;
  __dmb();
  uint16_t r = v->pref_r;
  uint32_t have = (uint16_t)((w + SAMPLE_SLOT_PREF_FRAMES - r) % SAMPLE_SLOT_PREF_FRAMES);
  if (have > max) have = max;
  int16_t *slot = pref_data(v);
  for (uint32_t n = 0; n < have; n++) {
    dst[n * 2] = slot[r * 2];
    dst[n * 2 + 1] = slot[r * 2 + 1];
    if (++r == SAMPLE_SLOT_PREF_FRAMES) r = 0;
  }
  __dmb();
  v->pref_r = r;
  return have;
}

static void sample_slot_voice_close(SampleSlotVoice *v) {
  if (v->fil.obj.fs) f_close(&v->fil);
  v->open = false;
  v->cue = false;
  pref_clear(v);
  slot_pull_voice = 0xff;
}

static bool slot_wav_8bit_22k(FIL *f, uint8_t *channels) {
  uint8_t h[44];
  UINT got = 0;
  if (f_lseek(f, 0) != FR_OK || f_read(f, h, 44, &got) != FR_OK || got != 44)
    return false;
  if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
  uint16_t ch = (uint16_t)(h[22] | (h[23] << 8));
  uint32_t rate = (uint32_t)h[24] | ((uint32_t)h[25] << 8) | ((uint32_t)h[26] << 16) |
                  ((uint32_t)h[27] << 24);
  uint16_t bits = (uint16_t)(h[34] | (h[35] << 8));
  if (ch != 1 || rate != 44100u || bits != 16) return false;
  *channels = 2;
  return true;
}

static int16_t slot_s16(const uint8_t *p) {
  return (int16_t)(p[0] | (p[1] << 8));
}

static void slot_pref_read(SampleSlotVoice *v, SampleInfo *info) {
  uint32_t frame_bytes = v->src_channels;
  if (!info || !info->size || !frame_bytes || v->seek || v->pref_eof) return;
  uint32_t limit = info->size;
  if (v->play_end && v->play_end < limit) limit = v->play_end;
  if (v->pref_at >= limit) {
    v->pref_eof = true;
    return;
  }
  if (pref_count(v) >= SAMPLE_SLOT_PREF_FRAMES - 1u) return;
  uint32_t remain = (limit - v->pref_at) / frame_bytes;
  uint32_t room = (SAMPLE_SLOT_PREF_FRAMES - 1u) - pref_count(v);
  uint32_t chunk = 4096u / frame_bytes;
  if (chunk > remain) chunk = remain;
  if (chunk > room) chunk = room;
  if (!chunk) return;
  uint32_t bytes = chunk * frame_bytes;
  static uint8_t raw[4096];
  uint32_t at = v->data_off + v->pref_at;
  UINT got = 0;
  bool read_ok;
  if (sample_slot_direct_ready()) {
    uint32_t n = 0;
    read_ok = sample_slot_direct_read(v->sample, at, raw, bytes, &n);
    got = (UINT)n;
  } else {
    bool placed = (uint32_t)f_tell(&v->fil) == at || f_lseek(&v->fil, at) == FR_OK;
    read_ok = placed && f_read(&v->fil, raw, (UINT)bytes, &got) == FR_OK;
    if (!read_ok || got < frame_bytes) {
      if (placed && f_eof(&v->fil)) v->pref_eof = true;
      else if ((uint32_t)f_tell(&v->fil) != at) f_lseek(&v->fil, at);
      return;
    }
  }
  uint32_t frames = read_ok ? got / frame_bytes : 0;
  if (!frames) return;
  if (v->pref_drop || !v->open) return;
  v->file_at = at + frames * frame_bytes;
  slot_pull_voice = 0xff;
  int16_t stereo[256 * 2];
  for (uint32_t base = 0; base < frames;) {
    uint32_t n = frames - base;
    if (n > 256u) n = 256u;
    for (uint32_t i = 0; i < n; i++) {
      int16_t s = slot_s16(raw + (base + i) * frame_bytes);
      stereo[i * 2] = s;
      stereo[i * 2 + 1] = s;
    }
    pref_push(v, stereo, n);
    base += n;
  }
  v->pref_at += frames * frame_bytes;
  if (v->pref_at >= limit) v->pref_eof = true;
}

static uint32_t slot_pref_cushion(SampleSlotVoice *v) {
  int8_t po = v->play_pitch;
  if (po < -12) po = -12;
  if (po > 12) po = 12;
  uint32_t need = (uint32_t)(882.0f * pitch_vals[PITCH_VAL_MID + po] + 4.0f);
  if (need < 64u) need = 64u;
  if (need > SAMPLE_SLOT_PREF_FRAMES) need = SAMPLE_SLOT_PREF_FRAMES;
  return need;
}

static void slot_next_grid(uint8_t *pattern, uint8_t *step) {
  uint8_t pat = sample_slot_pattern_sel;
  if (sample_slot_step == 15) {
    uint8_t chain_len = sample_slot_chain_len;
    bool empty = pat > 15 ||
                 !(sample_slot_pattern_mask & (uint16_t)(1u << pat));
    if (sample_slot_chain_pending) {
      pat = sample_slot_chain_len > 0 ? sample_slot_chain[0]
                                     : sample_slot_pattern_next;
    } else if (chain_len > 0 && !(empty && !sample_slot_chain_entered)) {
      uint8_t next_pos = 0;
      if (sample_slot_chain_entered) {
        next_pos = (uint8_t)(sample_slot_chain_pos + 1u);
        if (next_pos >= chain_len) next_pos = 0;
      }
      pat = sample_slot_chain[next_pos];
    } else if (sample_slot_pattern_next != pat) {
      pat = sample_slot_pattern_next;
    }
  }
  *pattern = pat;
  *step = (uint8_t)((sample_slot_step + 1) & 15);
}

static bool slot_note_ahead(const SampleSlotNote *n) {
  if ((!sample_slot_playing && !sample_slot_resample_busy) || n->pattern > 15)
    return false;
  if (n->pattern == sample_slot_pattern_sel && n->step == sample_slot_step &&
      n->nudge > sample_slot_ticks)
    return true;
  uint8_t pat, step;
  slot_next_grid(&pat, &step);
  return n->pattern == pat && n->step == step && n->nudge == 0;
}

static bool slot_cue_current(SampleSlotVoice *v) {
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->sample != v->sample || !slot_note_ahead(n)) continue;
    uint32_t phase = 0, end = 0;
    if (sample_slot_slice_mode[n->sample])
      sample_slot_region(n->sample, n->slice, true, &phase, &end);
    else
      sample_slot_region(n->sample, 0, false, &phase, &end);
    if (phase == v->phase) return true;
  }
  return false;
}

static bool slot_voice_engage(SampleSlotVoice *v) {
  int ord = sample_slot_ord(v->sample);
  if (ord < 0 || !banks[project_bank]) return false;
  bool same = v->fil.obj.fs && v->file_sample == v->sample && v->src_channels;
  if (sample_slot_direct_ready()) {
    uint8_t ch = 0;
    if (!sample_slot_format(v->sample, &ch)) return false;
    v->file_sample = v->sample;
    v->src_channels = ch;
    v->reserve = true;
    v->data_off = RESERVE_DATA_OFF;
    same = true;
  }
  if (!same) {
    if (v->fil.obj.fs) f_close(&v->fil);
    char name[32];
    format_sample_filename(name, project_bank, (uint8_t)ord, 0);
    uint8_t ch = 0;
    bool cached = sample_slot_format_cached(v->sample);
    bool known = cached && sample_slot_format(v->sample, &ch);
    if (cached && !known) return false;
    if (f_open(&v->fil, name, FA_READ) != FR_OK) return false;
    if (!cached && !slot_wav_8bit_22k(&v->fil, &ch)) {
      f_close(&v->fil);
      return false;
    }
    v->file_sample = v->sample;
    v->src_channels = ch;
    v->reserve = sample_slot_reserve_ok &&
                 sample_slot_reserve_load(v->sample, v->clmt);
    v->data_off = v->reserve ? RESERVE_DATA_OFF : 44u;
    v->fil.cltbl = v->reserve ? v->clmt : 0;
  }
  v->open = true;
  pref_clear(v);
  v->pref_at = v->phase;
  uint32_t at = v->data_off + v->phase;
  v->file_at = at;
  v->short_read = false;
  v->hold_run = 0;
  v->resume_fade = 0;
  if (!sample_slot_direct_ready() && f_lseek(&v->fil, at) != FR_OK) {
    v->seek = true;
    return true;
  }
  v->seek = false;
  v->pref_eof = false;
  v->primed = false;
  v->have_ahead = false;
  v->frac_q16 = 0;
  slot_pull_voice = 0xff;
  slot_pref_read(v, banks[project_bank]->sample[ord].snd[FILEZERO]);
  return true;
}

static int slot_cue_arm(void) {
  if (!sample_slot_playing && !sample_slot_resample_busy) return -1;
  for (uint8_t pass = 0; pass < 2; pass++)
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (!slot_note_ahead(n) || n->sample > 15) continue;
    bool current = n->pattern == sample_slot_pattern_sel &&
                   n->step == sample_slot_step;
    if (pass == 0 && !current) continue;
    if (pass == 1 && current) continue;
    uint32_t phase = 0, end = 0;
    if (sample_slot_slice_mode[n->sample])
      sample_slot_region(n->sample, n->slice, true, &phase, &end);
    else
      sample_slot_region(n->sample, 0, false, &phase, &end);
    bool primed = false;
    for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
      SampleSlotVoice *v = &sample_slot_voice[vi];
      if (v->sample != n->sample) continue;
      if (v->cue && v->phase == phase) primed = true;
    }
    if (primed) continue;
    int free_at = -1;
    for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
      SampleSlotVoice *v = &sample_slot_voice[vi];
      if (v->open || v->restart || v->cue) continue;
      if (free_at < 0) free_at = vi;
      if (v->fil.obj.fs && v->file_sample == n->sample) {
        free_at = vi;
        break;
      }
    }
    if (free_at < 0) continue;
    SampleSlotVoice *v = &sample_slot_voice[free_at];
    v->sample = n->sample;
    v->phase = phase;
    v->play_end = end;
    v->play_pitch = n->pitch;
    v->cue = true;
    v->restart = true;
    v->kill = false;
    v->defer_restart = false;
    return free_at;
  }
  return -1;
}

bool sample_slot_audio_short(void) {
  if (usb_record_blocks_card()) return false;
  for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
    SampleSlotVoice *v = &sample_slot_voice[vi];
    if (!v->open || v->cue || v->restart || !v->src_channels || v->seek || v->pref_eof)
      continue;
    if (v->kill && !v->release_left) continue;
    if (pref_count(v) < slot_pref_cushion(v)) return true;
  }
  return false;
}

#define SLOT_NEXT_BYTES 4096u
static uint8_t slot_next_raw[SAMPLE_SLOT_VOICES][SLOT_NEXT_BYTES];

static void slot_next_apply(SampleSlotVoice *v) {
  uint8_t vi = (uint8_t)(v - sample_slot_voice);
  uint32_t n = v->next_len & ~1u;
  if (vi >= SAMPLE_SLOT_VOICES || n < 2u) return;
  if (n > SLOT_NEXT_BYTES) n = SLOT_NEXT_BYTES;
  uint32_t frames = n / 2u;
  int16_t stereo[256 * 2];
  for (uint32_t base = 0; base < frames;) {
    uint32_t chunk = frames - base;
    if (chunk > 256u) chunk = 256u;
    for (uint32_t i = 0; i < chunk; i++) {
      int16_t s = slot_s16(slot_next_raw[vi] + (base + i) * 2u);
      stereo[i * 2] = s;
      stereo[i * 2 + 1] = s;
    }
    pref_push(v, stereo, chunk);
    base += chunk;
  }
  v->pref_at += n;
  v->next_on = false;
  v->next_need = false;
  v->next_len = 0;
}

#if AUDIO_EXTRA_OUTPUT_BUFFER
static unsigned audio_queued_blocks(void);
#endif

void sample_slot_voices_for_card(void) {
  for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
    SampleSlotVoice *v = &sample_slot_voice[vi];
    if (v->open || v->fil.obj.fs) sample_slot_voice_close(v);
    v->restart = false;
    v->kill = false;
    v->cue = false;
    v->seek = false;
    v->defer_restart = false;
    v->steal_after = false;
    v->release_left = 0;
  }
  sample_slot_sd_busy = false;
}

bool sample_slot_prefetch_service(void) {
  if (usb_record_blocks_card()) {
    for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
      SampleSlotVoice *v = &sample_slot_voice[vi];
      if (v->open || v->fil.obj.fs) sample_slot_voice_close(v);
      v->restart = false;
      v->kill = false;
    }
    usb_record_card_free();
    sample_slot_sd_busy = false;
    return false;
  }
  if (sample_slot_due && !sample_slot_resample_busy) return false;
#if AUDIO_EXTRA_OUTPUT_BUFFER
  if (!sample_slot_resample_busy && audio_queued_blocks() < 2) return false;
#endif
  bool busy = false;
  int close_at = -1;
  int open_at = -1;
  int seek_at = -1;
  int cue_open = -1;
  int cue_seek = -1;
  int cue_read = -1;
  int hungry_at = -1;
  uint32_t hungry_fill = 0xffffffffu;
  int top_at = -1;
  int next_at = -1;
  uint32_t top_fill = 0xffffffffu;
  for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
    SampleSlotVoice *v = &sample_slot_voice[vi];
    if (v->open) busy = true;
    if (sample_slot_resample_busy && sample_slot_resample_solo != 0xff &&
        vi != sample_slot_resample_solo)
      continue;
    if (close_at < 0 && v->cue && !v->restart && !slot_cue_current(v))
      close_at = vi;
    if (close_at < 0 && v->kill && !v->release_left &&
        (v->open || v->fil.obj.fs))
      close_at = vi;
    if (open_at < 0 && v->restart && !v->release_left && !v->cue &&
        (sample_slot_resample_busy ||
         (int32_t)(time_us_32() - v->card_after) >= 0))
      open_at = vi;
    if (next_at < 0 && v->next_need && v->open && !v->cue && !v->restart && !v->seek)
      next_at = vi;
    if (cue_open < 0 && v->cue && v->restart) cue_open = vi;
    if (seek_at < 0 && v->open && v->seek && !v->cue &&
        (sample_slot_resample_busy ||
         (int32_t)(time_us_32() - v->card_after) >= 0))
      seek_at = vi;
    if (v->cue && v->open && !v->restart) {
      if (v->seek && cue_seek < 0) cue_seek = vi;
      else if (!v->seek && !pref_count(v) && !v->pref_eof && cue_read < 0)
        cue_read = vi;
    }
    if (!v->open || v->cue || v->restart || !v->src_channels || v->seek || v->pref_eof)
      continue;
    if (v->kill && !v->release_left) continue;
    uint16_t queued = pref_count(v);
    uint32_t keep = SAMPLE_SLOT_PREF_FRAMES / 2u;
    if (queued < keep && queued < hungry_fill) {
      hungry_fill = queued;
      hungry_at = vi;
    } else if (queued < SAMPLE_SLOT_PREF_FRAMES - 1u && queued < top_fill) {
      top_fill = queued;
      top_at = vi;
    }
  }
  for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
    SampleSlotVoice *v = &sample_slot_voice[vi];
    if (!v->pref_drop) continue;
    pref_clear(v);
    v->pref_drop = false;
    if (!v->restart && !v->open) v->seek = false;
  }
  if (open_at < 0 && close_at < 0 && seek_at < 0 && hungry_at < 0 &&
      cue_seek < 0 && cue_read < 0 && next_at < 0) {
    if (cue_open >= 0) open_at = cue_open;
    else open_at = slot_cue_arm();
  }
  if (close_at >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[close_at];
    pref_clear(v);
    v->open = false;
    v->cue = false;
    v->restart = false;
    v->seek = false;
    v->kill = false;
    v->defer_restart = false;
    v->release_left = 0;
    sample_slot_sd_busy = busy;
    return true;
  } else if (hungry_at >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[hungry_at];
    uint32_t before = pref_count(v);
    int ord = sample_slot_ord(v->sample);
    if (ord >= 0)
      slot_pref_read(v, banks[project_bank]->sample[ord].snd[FILEZERO]);
    sample_slot_sd_busy = busy;
    return pref_count(v) > before || v->pref_eof;
  } else if (open_at >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[open_at];
    if (!slot_voice_engage(v)) {
      v->cue = false;
      v->restart = false;
      v->open = false;
    } else {
      v->restart = false;
      busy = true;
    }
    sample_slot_sd_busy = busy;
    return true;
  } else if (seek_at >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[seek_at];
    pref_clear(v);
    v->pref_at = v->phase;
    uint32_t at = v->data_off + v->phase;
    v->file_at = at;
    if (sample_slot_direct_ready() || f_lseek(&v->fil, at) == FR_OK) {
      slot_pull_voice = 0xff;
      v->seek = false;
      v->pref_eof = false;
      v->primed = false;
      v->have_ahead = false;
      v->frac_q16 = 0;
      v->hold_run = 0;
      v->resume_fade = 0;
      if (v->next_on && v->next_phase == v->phase && v->next_len) {
        slot_next_apply(v);
      } else if (sample_slot_direct_ready()) {
        int ord = sample_slot_ord(v->sample);
        if (ord >= 0)
          slot_pref_read(v, banks[project_bank]->sample[ord].snd[FILEZERO]);
      }
    }
    sample_slot_sd_busy = busy;
    return !v->seek;
  } else if (next_at >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[next_at];
    uint32_t got = 0;
    bool ok = sample_slot_direct_read(v->sample, v->data_off + v->next_phase,
                                      slot_next_raw[next_at], SLOT_NEXT_BYTES, &got);
    v->next_len = ok ? (uint16_t)got : 0;
    v->next_on = v->next_len > 0;
    v->next_need = !v->next_on;
    sample_slot_sd_busy = busy;
    return v->next_on;
  } else if (cue_seek >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[cue_seek];
    pref_clear(v);
    v->pref_at = v->phase;
    uint32_t at = v->data_off + v->phase;
    v->file_at = at;
    if (sample_slot_direct_ready() || f_lseek(&v->fil, at) == FR_OK) {
      slot_pull_voice = 0xff;
      v->seek = false;
      v->pref_eof = false;
      v->primed = false;
      v->have_ahead = false;
      v->frac_q16 = 0;
      v->hold_run = 0;
      v->resume_fade = 0;
      if (sample_slot_direct_ready()) {
        int ord = sample_slot_ord(v->sample);
        if (ord >= 0)
          slot_pref_read(v, banks[project_bank]->sample[ord].snd[FILEZERO]);
      }
    }
    sample_slot_sd_busy = busy;
    return !v->seek;
  } else if (cue_read >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[cue_read];
    uint32_t before = pref_count(v);
    int ord = sample_slot_ord(v->sample);
    if (ord >= 0)
      slot_pref_read(v, banks[project_bank]->sample[ord].snd[FILEZERO]);
    sample_slot_sd_busy = busy;
    return pref_count(v) > before || v->pref_eof;
  } else if (top_at >= 0) {
    SampleSlotVoice *v = &sample_slot_voice[top_at];
    uint32_t before = pref_count(v);
    int ord = sample_slot_ord(v->sample);
    if (ord >= 0)
      slot_pref_read(v, banks[project_bank]->sample[ord].snd[FILEZERO]);
    sample_slot_sd_busy = busy;
    return pref_count(v) > before || v->pref_eof;
  }
  sample_slot_sd_busy = busy;
  return false;
}

static void sample_slot_resample_clock(SampleSlotVoice *v, uint32_t frames) {
  if (v->restart) {
    v->restart = false;
    v->open = true;
    v->seek = false;
    v->cue = false;
    if (!v->src_channels) v->src_channels = 2;
  }
  if (!v->open || v->cue) return;
  int8_t po = v->play_pitch;
  if (po < -12) po = -12;
  if (po > 12) po = 12;
  uint32_t step = (uint32_t)(pitch_vals[PITCH_VAL_MID + po] * 65536.0f + 0.5f);
  if (!step) step = 65536;
  uint32_t src = (uint32_t)(((uint64_t)step * frames) >> 16);
  if (!src) src = 1;
  uint32_t fb = v->src_channels ? v->src_channels : 2u;
  v->phase += src * fb;
  if (v->release_left) {
    if (v->release_left > frames) v->release_left -= frames;
    else v->release_left = 0;
  } else if (!v->release_left && v->defer_restart) {
    if (v->steal_after) {
      v->sample = v->pending_sample;
      v->steal_after = false;
    }
    v->phase = v->pending_phase;
    v->play_end = v->pending_end;
    v->defer_restart = false;
    v->attack_pos = 0;
    v->decay_pos = 0;
    return;
  } else if (v->decay_len) {
    uint32_t n = frames;
    if (v->attack_pos < v->attack_len) {
      uint32_t rise = v->attack_len - v->attack_pos;
      if (rise > n) rise = n;
      v->attack_pos = (uint16_t)(v->attack_pos + rise);
      n -= rise;
    }
    if (n && v->decay_pos < v->decay_len) {
      uint32_t fall = v->decay_len - v->decay_pos;
      if (fall > n) fall = n;
      v->decay_pos = (uint16_t)(v->decay_pos + fall);
      n -= fall;
    }
    if (n && v->decay_pos >= v->decay_len) {
      v->open = false;
      v->kill = false;
      v->release_left = 0;
      return;
    }
  }
  int ord = sample_slot_ord(v->sample);
  uint32_t limit = 0;
  if (ord >= 0 && banks[project_bank]) {
    SampleInfo *info = banks[project_bank]->sample[ord].snd[FILEZERO];
    if (info) limit = info->size;
  }
  if (v->play_end && (!limit || v->play_end < limit)) limit = v->play_end;
  if (limit && v->phase >= limit && !v->release_left) {
    v->open = false;
    v->kill = false;
  }
}

static void __not_in_flash_func(sample_slot_render_voices)(int32_t *dst,
                                                            uint32_t frames,
                                                            int32_t gain) {
  memset(dst, 0, frames * 2 * sizeof(int32_t));
  if (usb_record_blocks_card()) return;
  static int16_t pcm[(441 * 3 / 2 + 8) * 2];
  for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
    SampleSlotVoice *v = &sample_slot_voice[vi];
    if (sample_slot_resample_busy && sample_slot_resample_solo != 0xff &&
        vi != sample_slot_resample_solo) {
      sample_slot_resample_clock(v, frames);
      continue;
    }
    int ord = sample_slot_ord(v->sample);
    int8_t pitch = v->play_pitch;
    int8_t cutoff = v->play_filter;
    uint8_t resonance = v->play_filter_q;
    if (v->kill && !v->release_left) continue;
    if (v->cue || v->restart || v->seek || !v->open || ord < 0) continue;
    SampleInfo *info = banks[project_bank]->sample[ord].snd[FILEZERO];
    if (!info || !info->size) continue;
    if (!v->src_channels) continue;
    if (v->attack_pos == 0 && v->decay_pos == 0 && !v->release_left && !v->pref_eof) {
      int8_t po = pitch;
      if (po < -12) po = -12;
      if (po > 12) po = 12;
      uint32_t begin_q16 =
          (uint32_t)(pitch_vals[PITCH_VAL_MID + po] * 65536.0f + 0.5f);
      if (begin_q16 == 0) begin_q16 = 65536;
      uint32_t need = (uint32_t)(((uint64_t)begin_q16 * frames) >> 16);
      if (need < 1u) need = 1u;
      if (pref_count(v) < need) continue;
    }
    uint32_t frame_bytes = v->src_channels;
    if (frames > 512) frames = 512;
    uint32_t dst_at = 0;
    uint32_t todo = frames;
    while (todo) {
      bool ending = v->phase >= info->size || (v->play_end && v->phase >= v->play_end);
      if (ending && v->defer_restart && !v->steal_after) {
        v->release_left = 0;
        v->phase = v->pending_phase;
        v->play_end = v->pending_end;
        v->defer_restart = false;
        v->seek = true;
        v->primed = false;
        v->have_ahead = false;
        v->frac_q16 = 0;
        v->hold_run = 0;
        v->resume_fade = 0;
        v->attack_pos = 0;
        v->decay_pos = 0;
        v->short_read = false;
        v->filter_reset = true;
        if (v->pending_velocity) v->play_velocity = v->pending_velocity;
        v->release_len = sample_slot_release[v->sample]
                             ? sample_slot_release[v->sample]
                             : SAMPLE_SLOT_ENV_MIN;
        ending = v->phase >= info->size || (v->play_end && v->phase >= v->play_end);
      }
      if (ending && v->play_end && v->phase >= v->play_end && !v->release_left &&
          !(v->decay_len && v->decay_pos >= v->decay_len)) {
        v->release_left = v->release_len ? v->release_len : SAMPLE_SLOT_ENV_MIN;
        v->kill = true;
        v->play_end = 0;
        ending = v->phase >= info->size;
      }
      if (ending && v->release_left) {
        uint32_t span = todo;
        if (v->release_left < span) span = v->release_left;
        uint32_t den = v->release_len ? v->release_len : 1;
        for (uint32_t i = 0; i < span; i++) {
          uint32_t fall = (uint32_t)v->release_left - 1;
          int32_t left = (int32_t)(((int64_t)v->hold_l << 16) * fall / den);
          int32_t right = (int32_t)(((int64_t)v->hold_r << 16) * fall / den);
          left = q16_16_multiply(left, gain);
          right = q16_16_multiply(right, gain);
          left = (int32_t)((int64_t)left * v->play_velocity / 127);
          right = (int32_t)((int64_t)right * v->play_velocity / 127);
          dst[(dst_at + i) * 2] += left;
          dst[(dst_at + i) * 2 + 1] += right;
          v->release_left--;
        }
        dst_at += span;
        todo -= span;
        if (!v->release_left) {
          v->open = false;
          v->kill = false;
          break;
        }
        continue;
      }
      if (ending) {
        v->open = false;
        break;
      }
      if (v->seek) break;
      int8_t po = pitch;
      if (po < -12) po = -12;
      if (po > 12) po = 12;
      uint32_t step_q16 =
          (uint32_t)(pitch_vals[PITCH_VAL_MID + po] * 65536.0f + 0.5f);
      if (step_q16 == 0) step_q16 = 65536;

      if (!v->primed) {
        if (v->phase >= info->size || (v->play_end && v->phase >= v->play_end)) {
          v->open = false;
          break;
        }
        int16_t one[2] = {0, 0};
        uint32_t got_frames = 0;
        if (pref_take(v, one, 1)) got_frames = 1;
        if (!got_frames) break;
        v->short_read = false;
        v->phase += frame_bytes;
        v->hold_l = one[0];
        v->hold_r = one[1];
        v->primed = true;
        v->frac_q16 = 0;
        v->hold_run = 0;
        v->resume_fade = 0;
        v->have_ahead = false;
      }

      uint32_t span = todo;
      if (v->release_left && v->release_left < span) span = v->release_left;
      uint32_t frac = v->frac_q16;
      uint64_t end = (uint64_t)frac + (uint64_t)step_q16 * span;
      uint32_t consumed = (uint32_t)(end >> 16);
      uint32_t new_frac = (uint32_t)(end & 0xFFFF);
      uint64_t last = span ? (uint64_t)frac + (uint64_t)step_q16 * (span - 1)
                           : frac;
      uint32_t need_after = (uint32_t)(last >> 16) + 1;
      const uint32_t pcm_frames = (sizeof pcm) / (2 * sizeof(int16_t));
      if (need_after > pcm_frames) need_after = pcm_frames;

      uint32_t filled = 0;
      if (v->have_ahead && need_after > 0) {
        pcm[0] = v->ahead_l;
        pcm[1] = v->ahead_r;
        filled = 1;
        v->have_ahead = false;
      }
      bool stopped = false;
      bool starved = false;
      while (filled < need_after && !stopped) {
        if (v->phase >= info->size || (v->play_end && v->phase >= v->play_end)) {
          stopped = true;
          break;
        }
        uint32_t limit = info->size;
        if (v->play_end && v->play_end < limit) limit = v->play_end;
        uint32_t remain = (limit - v->phase) / frame_bytes;
        uint32_t chunk = need_after - filled;
        if (chunk > remain) chunk = remain;
        if (chunk == 0) break;
        uint32_t have = pref_take(v, pcm + filled * 2, chunk);
        if (have < chunk) {
          if (have) {
            filled += have;
            v->phase += have * frame_bytes;
          }
          stopped = true;
          starved = !v->pref_eof;
          break;
        }
        v->short_read = false;
        filled += have;
        v->phase += have * frame_bytes;
      }

      static int32_t voice_buf[512 * 2];
      memset(voice_buf, 0, span * 2 * sizeof(int32_t));
      uint32_t acc = frac;
      uint32_t produced = 0;
      uint32_t real_out = 0;
      bool cut = false;
      int16_t tail_l = v->hold_l;
      int16_t tail_r = v->hold_r;
      int16_t freeze_l = v->hold_l;
      int16_t freeze_r = v->hold_r;
      for (uint32_t i = 0; i < span; i++) {
        uint32_t idx = acc >> 16;
        uint32_t sub = acc & 0xFFFF;
        bool held = false;
        int16_t a_l;
        int16_t a_r;
        int16_t b_l;
        int16_t b_r;
        if (idx > filled) {
          cut = true;
          break;
        } else {
          a_l = idx == 0 ? v->hold_l : pcm[(idx - 1) * 2];
          a_r = idx == 0 ? v->hold_r : pcm[(idx - 1) * 2 + 1];
          uint32_t idx_b = idx + 1;
          b_l = a_l;
          b_r = a_r;
          if (idx_b == 0) {
            b_l = v->hold_l;
            b_r = v->hold_r;
          } else if (idx_b <= filled) {
            b_l = pcm[(idx_b - 1) * 2];
            b_r = pcm[(idx_b - 1) * 2 + 1];
          }
        }
        int32_t raw_l = ((int32_t)a_l << 16) + ((int32_t)(b_l - a_l) * sub);
        int32_t raw_r = ((int32_t)a_r << 16) + ((int32_t)(b_r - a_r) * sub);
        if (!held) {
          if (v->hold_run > 88u) v->resume_fade = 88u;
          if (v->resume_fade) {
            uint32_t keep = v->resume_fade;
            uint32_t gone = 88u - keep;
            raw_l = (int32_t)(((int64_t)v->hold_l * keep + (int64_t)(raw_l >> 16) * gone) << 16) / 88;
            raw_r = (int32_t)(((int64_t)v->hold_r * keep + (int64_t)(raw_r >> 16) * gone) << 16) / 88;
            v->resume_fade--;
          }
          freeze_l = (int16_t)(raw_l >> 16);
          freeze_r = (int16_t)(raw_r >> 16);
          real_out++;
          v->hold_run = 0;
        } else if (v->hold_run < 65535u) {
          v->hold_run++;
        }
        if (held && v->hold_run > 88u) {
          raw_l = 0;
          raw_r = 0;
        } else if (v->release_left) {
          uint32_t fall = (uint32_t)v->release_left - 1;
          uint32_t den = v->release_len ? v->release_len : 1;
          raw_l = (int32_t)((int64_t)raw_l * fall / den);
          raw_r = (int32_t)((int64_t)raw_r * fall / den);
          v->release_left--;
        } else if (!held && v->attack_len && v->attack_pos < v->attack_len) {
          uint32_t rise = (uint32_t)v->attack_pos + 1;
          raw_l = (int32_t)((int64_t)raw_l * rise / v->attack_len);
          raw_r = (int32_t)((int64_t)raw_r * rise / v->attack_len);
          v->attack_pos++;
        } else if (!held && v->decay_len) {
          if (v->decay_pos < v->decay_len) {
            uint32_t fall = (uint32_t)v->decay_len - v->decay_pos;
            raw_l = (int32_t)((int64_t)raw_l * fall / v->decay_len);
            raw_r = (int32_t)((int64_t)raw_r * fall / v->decay_len);
            v->decay_pos++;
          } else {
            v->open = false;
            v->kill = false;
            v->release_left = 0;
            v->pref_drop = true;
            cut = true;
            break;
          }
        }
        tail_l = (int16_t)(raw_l >> 16);
        tail_r = (int16_t)(raw_r >> 16);
        int32_t left = q16_16_multiply(raw_l, gain);
        int32_t right = q16_16_multiply(raw_r, gain);
        left = (int32_t)((int64_t)left * v->play_velocity / 127);
        right = (int32_t)((int64_t)right * v->play_velocity / 127);
        voice_buf[i * 2] = left;
        voice_buf[i * 2 + 1] = right;
        produced++;
        if (!held) acc += step_q16;
      }
      uint32_t voiced = cut ? produced : span;
      if (voiced) {
        sample_slot_filter_voice(v, voice_buf, voiced, cutoff, resonance);
        for (uint32_t i = 0; i < voiced; i++) {
          dst[(dst_at + i) * 2] += voice_buf[i * 2];
          dst[(dst_at + i) * 2 + 1] += voice_buf[i * 2 + 1];
        }
      }
      dst_at += voiced;
      todo -= voiced;
      if (!v->open) break;
      if (starved) {
        v->open = false;
        v->cue = false;
        v->restart = false;
        v->seek = false;
        v->kill = false;
        v->release_left = 0;
        v->pref_drop = true;
        break;
      } else if (cut) {
        v->hold_l = tail_l;
        v->hold_r = tail_r;
        if (!produced &&
            !(v->phase >= info->size || (v->play_end && v->phase >= v->play_end)))
          break;
      } else if (!(stopped && !v->open)) {
        if (consumed == 0) {
          v->frac_q16 = new_frac;
        } else if (consumed <= filled) {
          v->hold_l = pcm[(consumed - 1) * 2];
          v->hold_r = pcm[(consumed - 1) * 2 + 1];
          v->frac_q16 = new_frac;
          if (filled == consumed + 1) {
            v->ahead_l = pcm[consumed * 2];
            v->ahead_r = pcm[consumed * 2 + 1];
            v->have_ahead = true;
          }
        } else {
          uint64_t endp = (uint64_t)frac + (uint64_t)step_q16 * produced;
          v->frac_q16 = (uint32_t)(endp & 0xFFFF);
          v->hold_l = tail_l;
          v->hold_r = tail_r;
        }
      }
      if (!v->release_left && v->kill) {
        v->open = false;
        v->kill = false;
        break;
      }
      if (!v->release_left && v->defer_restart) {
        v->phase = v->pending_phase;
        v->play_end = v->pending_end;
        v->defer_restart = false;
        if (v->steal_after) {
          v->sample = v->pending_sample;
          v->steal_after = false;
          v->restart = true;
          v->seek = false;
          break;
        }
        v->seek = true;
        v->primed = false;
        v->have_ahead = false;
        v->frac_q16 = 0;
        v->hold_run = 0;
        v->resume_fade = 0;
        v->attack_pos = 0;
        v->decay_pos = 0;
        v->short_read = false;
        v->filter_reset = true;
        if (v->pending_velocity) v->play_velocity = v->pending_velocity;
        v->release_len = sample_slot_release[v->sample]
                             ? sample_slot_release[v->sample]
                             : SAMPLE_SLOT_ENV_MIN;
        if (sample_slot_resample_busy) {
          pref_clear(v);
          v->pref_at = v->phase;
          v->file_at = v->data_off + v->phase;
          v->seek = false;
          v->pref_eof = false;
          int ord_now = sample_slot_ord(v->sample);
          if (ord_now >= 0 && banks[project_bank])
            slot_pref_read(v, banks[project_bank]->sample[ord_now].snd[FILEZERO]);
        }
      } else if (!starved && !cut && (stopped || !v->open)) {
        break;
      }
    }
  }
  sample_slot_sd_busy = false;
  uint32_t playhead = 0;
  uint8_t play_slot = sample_slot_edit;
  bool play_found = false;
  uint32_t newest = 0;
  for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
    SampleSlotVoice *v = &sample_slot_voice[vi];
    if (v->open) sample_slot_sd_busy = true;
    if (v->cue || (!v->open && !v->restart)) continue;
    if (v->sample == sample_slot_edit) {
      playhead = v->phase;
      play_slot = v->sample;
      play_found = true;
    } else if (!play_found && v->started >= newest) {
      newest = v->started;
      playhead = v->phase;
      play_slot = v->sample;
    }
  }
  display_set_playhead(play_slot, playhead);
}

int32_t *sample_slot_resample_mix(uint32_t frames) {
  static int32_t sample_slot_resample_samples[128 * 2];
  if (frames > 128) frames = 128;
  for (uint8_t spin = 0; spin < 16; spin++) {
    bool need = false;
    for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
      SampleSlotVoice *v = &sample_slot_voice[vi];
      if (v->restart || v->seek) {
        need = true;
        break;
      }
      if (!v->open || v->cue || v->pref_eof || (v->kill && !v->release_left))
        continue;
      int8_t po = v->play_pitch;
      if (po < -12) po = -12;
      if (po > 12) po = 12;
      uint32_t step_q16 =
          (uint32_t)(pitch_vals[PITCH_VAL_MID + po] * 65536.0f + 0.5f);
      if (step_q16 == 0) step_q16 = 65536;
      uint32_t want = (uint32_t)(((uint64_t)step_q16 * frames) >> 16) + 2u;
      if (pref_count(v) < want) {
        need = true;
        break;
      }
    }
    if (!need || !sample_slot_prefetch_service()) break;
  }
  sample_slot_render_voices(sample_slot_resample_samples, frames,
                            volume_vals[project_vol]);
  return sample_slot_resample_samples;
}

static void __not_in_flash_func(zeptocore_render_audio)() {
  audio_buffer_t *buffer = take_audio_buffer(ap, false);
  if (buffer == NULL) {
    ZD_CALL(zd_audio_counter(ZD_NO_BUFFER));
    return;
  }

  int32_t *samples = audio_render_samples;
  ZD_WITNESS_RENDERED();
  int16_t *samples16 = (int16_t *)buffer->buffer->bytes;

  if (sample_slot_resample_busy) {
    memset(samples, 0, buffer->max_sample_count * 2 * sizeof(int32_t));
  } else if (mode_buttons16 == MODE_SAMPLE_SLOT) {
    sample_slot_render_voices(samples, buffer->max_sample_count,
                              volume_vals[project_vol]);
    usb_audio_mix(samples, buffer->max_sample_count, volume_vals[project_vol]);
  } else {
    memset(samples, 0, buffer->max_sample_count * 2 * sizeof(int32_t));
  }
  buffer->sample_count = buffer->max_sample_count;
  static int32_t main_limit_gain = 65536;
  for (uint16_t i = 0; i < buffer->max_sample_count; i++) {
    int32_t left = samples[i * 2 + 0] >> 16;
    int32_t right = samples[i * 2 + 1] >> 16;
    int32_t peak = left >= 0 ? left : -left;
    int32_t other = right >= 0 ? right : -right;
    if (other > peak) peak = other;
    int32_t target = 65536;
    if (peak > 28000) target = (int32_t)((28000ll * 65536) / peak);
    if (target < main_limit_gain)
      main_limit_gain = target;
    else if (main_limit_gain < 65536) {
      main_limit_gain += (65536 - main_limit_gain) >> 11;
      if (main_limit_gain > 65536) main_limit_gain = 65536;
    }
    left = (int32_t)(((int64_t)left * main_limit_gain) >> 16);
    right = (int32_t)(((int64_t)right * main_limit_gain) >> 16);
    if (left > 32767) left = 32767;
    if (left < -32768) left = -32768;
    if (right > 32767) right = 32767;
    if (right < -32768) right = -32768;
    samples16[i * 2 + 0] = (int16_t)left;
    samples16[i * 2 + 1] = (int16_t)right;
  }
  audio_submit_buffer(buffer, false);
}


// All returns from the renderer pass through measurement/publication.
#if AUDIO_DETAILED_TIMING
static uint32_t audio_profile_effects(void) {
  uint32_t mask=0;
#ifndef INCLUDE_ZEPTOCORE
  if(sf)for(unsigned i=0;i<16;++i)mask|=sf->fx_active[i]?1u<<i:0;
#endif
  return mask;
}
static uint32_t audio_profile_source(bool owns) {
  return (sel_bank_cur<<12)|(metadata_filename_index(sel_bank_cur,sel_sample_cur)<<8)|(sel_variation+audio_variant*2)|
      ((owns&&fil_current.cltbl)?1u<<30:0)|(audio_callback_in_mute?1u<<31:0);
}
#endif
#if AUDIO_EXTRA_OUTPUT_BUFFER
static volatile uint32_t audio_queue_refills;
static unsigned audio_queued_blocks(void) {
  if(!ap)return 0;
  uint32_t irq=spin_lock_blocking(ap->prepared_list_spin_lock);
  unsigned count=0;
  for(audio_buffer_t *b=ap->prepared_list;b&&count<4;b=b->next)++count;
  spin_unlock(ap->prepared_list_spin_lock,irq);
  return count;
}
#endif
void __not_in_flash_func(i2s_callback_func)() {
  ZD_CALL(zd_audio_begin());
  bool owns_media=audio_media_begin();
  AP_CALL(audio_profile_begin(owns_media&&zeptocore_diag.header[21]>=3,
      audio_profile_source(owns_media),audio_profile_effects(),
#ifdef INCLUDE_ZEPTOCORE
      0,0));
#else
      sf?sf->pitch_val_index:0,0));
#endif
  if(owns_media && !bank_transition_audio_hold() &&
     (metadata_ready(sel_bank_cur) || mode_buttons16 == MODE_SAMPLE_SLOT)) {
    zeptocore_render_audio();
#if AUDIO_EXTRA_OUTPUT_BUFFER
    // Maintain one queued block beyond the ordinary next block. A startup
    // reserve alone can drain and never recover with one render per DMA event.
    // No filesystem work occurs under the queue's short list lock.
    while (ap && audio_queued_blocks() < 4) {
      unsigned before = audio_queued_blocks();
      ++audio_queue_refills;
      zeptocore_render_audio();
      if (audio_queued_blocks() <= before) break;
    }
#endif
  } else if(ap) {
    audio_buffer_t *buffer=take_audio_buffer(ap,false);
    if(buffer) {
      ZD_WITNESS_RENDERED();
      memset(buffer->buffer->bytes,0,buffer->max_sample_count*4);
      buffer->flags = AUDIO_BUFFER_SILENCE;
      buffer->sample_count=buffer->max_sample_count;
      ZD_CALL(zd_audio_output(buffer->sample_count,0));
      ZD_CALL(zd_audio_counter(ZD_MUTED));
      audio_submit_buffer(buffer, false);
    } else ZD_CALL(zd_audio_counter(ZD_NO_BUFFER));
  }
  AP_CALL(audio_profile_end(audio_profile_source(owns_media),audio_profile_effects()));
  ZV_CALL({
    uint32_t trigger = zv_trigger_read();
    uint8_t bank = sel_bank_cur, sample = sel_sample_cur;
    uint8_t slice = trigger & 255;
    bool valid = owns_media && metadata_ready(bank) && !bank_transition_busy() && fil_is_open && bank < 16 && banks[bank] &&
        banks[bank]->sample && sample < banks[bank]->num_samples &&
        !fil_current_change && !fil_current_change_force && !do_open_file_ready;
    // A newly opened sample must never inherit the previous sample's trigger.
    valid = valid && ((trigger >> 12) & 15) == bank &&
        ((trigger >> 8) & 15) == metadata_filename_index(bank,sample);
    if (valid) {
      SampleInfo *sound = banks[bank]->sample[sample].snd[FILEZERO];
      valid = sound && sound->slice_start && sound->slice_stop && slice < sound->slice_num;
    }
    zv_snapshot snapshot = {.bank = bank, .sample = metadata_filename_index(bank,sample), .slice = valid ? slice : 0,
        .trigger = trigger >> 16, .bpm = owns_media ? project_bpm : 0,
        .forward = phase_forward, .stopped = playback_stopped || do_stop_playback,
        .muted = button_mute || trigger_button_mute, .valid = valid};
#ifndef INCLUDE_ZEPTOCORE
    if (valid && sf) {
      for (unsigned i = 0; i < 16; ++i)
        if (sf->fx_active[i]) snapshot.effects |= (uint16_t)(1u << i);
    }
#endif
    zv_publish(&snapshot);
  });
  ZD_CALL(if(zeptocore_diag.request.sequence!=zeptocore_diag.audio.header.sequence) {
    zd_audio.counters[12]=audio_file_generation;
    zd_audio.counters[13]=seek_maps_stats.hits;
    zd_audio.counters[14]=seek_maps_stats.misses;
    zd_audio.counters[15]=audio_media_stats.withheld_callbacks;
    zd_audio.context[15]=owns_media?fil_is_open:2;
  });
  ZD_CALL(if (owns_media && zeptocore_diag.request.sequence !=
                 zeptocore_diag.audio.header.sequence && fil_is_open) {
    uint32_t *c = zd_audio.context;
    c[0] = sel_bank_cur; c[1] = metadata_filename_index(sel_bank_cur,sel_sample_cur);
    c[2] = sel_variation; c[3] = audio_variant; c[4] = phase_forward;
    c[5] = 0;
#ifdef INCLUDE_ZEPTOCORE
    c[6] = 0; c[7] = project_bpm; c[8] = 0;
#else
    c[6] = sf->pitch_val_index;
    c[7] = sf->bpm_tempo; c[8] = 0;
    for (unsigned i = 0; i < 16; ++i) c[8] |= sf->fx_active[i] ? 1u << i : 0;
#endif
    c[9] = f_size(&fil_current); c[10] = f_size(&fil_current) >> 32;
    c[11] = fil_current.obj.sclust;
    c[12] = f_tell(&fil_current); c[13] = f_tell(&fil_current) >> 32;
    c[14] = fil_current.cltbl != NULL; c[15] = fil_is_open;
  });
  ZD_CALL(zd_audio_end());
  bool quiescent=false;
  // Optional map work may use a stopped, already muted output with no effect
  // tails. A mute flag alone cannot establish this, or filesystem ownership.
  quiescent=owns_media&&playback_stopped&&audio_callback_in_mute;
  audio_media_end(quiescent);
}

bool i2s_callback_restart_func(void) {
#if AUDIO_RESTART_WAKE_ENABLED
  if (!ap || !audio_restart_pending() || !audio_restart_can_wake(ap)) return false;
  audio_restart_wake.active = true;
  audio_restart_wake.published = false;
  audio_restart_wake.replaced_silence = 0;
  i2s_callback_func();
  audio_restart_wake.active = false;
  // If the sole queued silence was consumed during this render, its DMA
  // notification is still pending on this same core's FIFO. This extra render
  // fulfilled that one notification. Do not change the normal queue policy.
  return audio_restart_wake.published && audio_restart_wake.replaced_silence == 0;
#else
  return false;
#endif
}
