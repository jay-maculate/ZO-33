#include "usb_record.h"
#include "bank_metadata.h"
#include "ff.h"
#include "project.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "diskio.h"
#include "hw_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REC_MAX_FRAMES (20u * 44100u)
#define REC_THRESHOLD 2067
/* Same frame count as the old delay, comb, and beat-repeat rings. */
#define REC_CAP_FRAMES 25525u

enum { REC_IDLE, REC_ARMED, REC_CAPTURE, REC_RECORD, REC_FINISH };

int16_t sample_slot_pref_mem[REC_CAP_FRAMES * 2u];
_Static_assert(4u * SAMPLE_SLOT_PREF_FRAMES * 2u <= REC_CAP_FRAMES * 2u,
               "sample queues exceed the shared record buffer");
#define rec_samples sample_slot_pref_mem
static const uint32_t rec_cap = REC_CAP_FRAMES;
static volatile uint32_t rec_w;
static volatile uint32_t rec_r;
static volatile uint8_t rec_state;
static volatile uint8_t rec_overflow;
static volatile uint8_t rec_block;
static volatile uint32_t rec_epoch;
static uint32_t rec_epoch_seen;
static uint32_t rec_full_until;
static uint8_t rec_slot;
static volatile uint32_t rec_frames;
static uint32_t rec_written;
static volatile int32_t rec_peak;
static int32_t rec_mix_peak;
static FIL rec_file;
static bool rec_open;

static void rec_store(uint32_t index, int16_t left, int16_t right) {
  rec_samples[index * 2u] = left;
  rec_samples[index * 2u + 1u] = right;
}

static void rec_load(uint32_t index, int16_t *left, int16_t *right) {
  if (index >= rec_cap) {
    *left = 0;
    *right = 0;
    return;
  }
  *left = rec_samples[index * 2u];
  *right = rec_samples[index * 2u + 1u];
}

static bool rec_push(int16_t left, int16_t right) {
  if (!rec_cap || rec_frames >= REC_MAX_FRAMES) return false;
  uint32_t next = rec_w + 1;
  if (next == rec_cap) next = 0;
  if (next == rec_r) {
    rec_overflow = 1;
    return false;
  }
  rec_store(rec_w, left, right);
  int32_t a = left >= 0 ? left : -(int32_t)left;
  int32_t b = right >= 0 ? right : -(int32_t)right;
  if (b > a) a = b;
  if (a > rec_peak) rec_peak = a;
  __dmb();
  rec_w = next;
  rec_frames++;
  return true;
}

bool sample_slot_reserve_ok;

static uint32_t rd_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static bool reserve_info_logical(FIL *f, uint32_t *logical) {
  uint8_t h[11];
  UINT got = 0;
  if (f_size(f) < 11 || f_lseek(f, 0) != FR_OK || f_read(f, h, 11, &got) != FR_OK ||
      got != 11)
    return false;
  uint32_t slice = h[10];
  uint8_t version = (rd_le32(h + 4) >> 16) & 127u;
  if (!slice) return false;
  uint32_t len = 11u + 9u * slice;
  if (version) {
    uint8_t c[6];
    if (f_lseek(f, len) != FR_OK || f_read(f, c, 6, &got) != FR_OK || got != 6)
      return false;
    uint32_t n1 = (uint32_t)c[0] | ((uint32_t)c[1] << 8);
    uint32_t n2 = (uint32_t)c[2] | ((uint32_t)c[3] << 8);
    uint32_t n3 = (uint32_t)c[4] | ((uint32_t)c[5] << 8);
    len = 17u + 9u * slice + 2u * (n1 + n2 + n3);
  }
  if (len > f_size(f)) return false;
  *logical = len;
  return true;
}

static bool reserve_read_trailer(FIL *f, uint32_t *clmt) {
  uint32_t logical = 0;
  if (!reserve_info_logical(f, &logical)) return false;
  uint8_t head[8];
  UINT got = 0;
  if (f_lseek(f, logical) != FR_OK || f_read(f, head, 8, &got) != FR_OK || got != 8)
    return false;
  if (head[0] != 'C' || head[1] != 'L') return false;
  uint16_t items = (uint16_t)(head[2] | (head[3] << 8));
  if (rd_le32(head + 4) != RESERVE_DATA_OFF || items < 3 || items > RESERVE_CLMT)
    return false;
  if (f_read(f, clmt, items * 4u, &got) != FR_OK || got != items * 4u) return false;
  return clmt[0] == items && items > 0 && clmt[items - 1u] == 0;
}

static bool reserve_slot_sized(unsigned bank, unsigned slot) {
  char name[40];
  FILINFO st;
  snprintf(name, sizeof name, "bank%u/%u.0.wav", bank + 1, slot);
  return f_stat(name, &st) == FR_OK && st.fsize == RESERVE_SLOT_BYTES;
}

static bool reserve_slot_ok(unsigned bank, unsigned slot, uint32_t *clmt) {
  char name[40];
  if (!reserve_slot_sized(bank, slot)) return false;
  snprintf(name, sizeof name, "bank%u/%u.0.wav.info", bank + 1, slot);
  FIL info;
  if (f_open(&info, name, FA_READ) != FR_OK) return false;
  bool ok = reserve_read_trailer(&info, clmt);
  f_close(&info);
  return ok;
}

static bool reserve_write_trailer(unsigned bank, unsigned slot, const uint32_t *clmt) {
  char name[40];
  FIL info;
  UINT wrote = 0;
  uint32_t logical = 0;
  snprintf(name, sizeof name, "bank%u/%u.0.wav.info", bank + 1, slot);
  if (f_open(&info, name, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) return false;
  if (!reserve_info_logical(&info, &logical)) {
    uint8_t body[20];
    uint32_t flags = 120u;
    memset(body, 0, sizeof body);
    body[4] = (uint8_t)flags;
    body[5] = (uint8_t)(flags >> 8);
    body[6] = (uint8_t)(flags >> 16);
    body[7] = (uint8_t)(flags >> 24);
    body[8] = 2;
    body[10] = 1;
    if (f_lseek(&info, 0) != FR_OK || f_write(&info, body, 20, &wrote) != FR_OK || wrote != 20) {
      f_close(&info);
      return false;
    }
    logical = 20;
  }
  uint16_t items = (uint16_t)clmt[0];
  uint8_t head[8] = {'C', 'L', (uint8_t)items, (uint8_t)(items >> 8),
                     (uint8_t)RESERVE_DATA_OFF, (uint8_t)(RESERVE_DATA_OFF >> 8),
                     (uint8_t)(RESERVE_DATA_OFF >> 16), (uint8_t)(RESERVE_DATA_OFF >> 24)};
  bool ok = f_lseek(&info, logical) == FR_OK && f_write(&info, head, 8, &wrote) == FR_OK &&
            wrote == 8 && f_write(&info, clmt, items * 4u, &wrote) == FR_OK &&
            wrote == items * 4u && f_truncate(&info) == FR_OK && f_sync(&info) == FR_OK;
  f_close(&info);
  return ok;
}

static bool reserve_build_map(unsigned bank, unsigned slot, uint32_t *clmt) {
  char name[40];
  FIL wav;
  snprintf(name, sizeof name, "bank%u/%u.0.wav", bank + 1, slot);
  if (f_open(&wav, name, FA_READ) != FR_OK) return false;
  clmt[0] = RESERVE_CLMT - 1u;
  wav.cltbl = clmt;
  FRESULT fr = f_lseek(&wav, CREATE_LINKMAP);
  wav.cltbl = 0;
  f_close(&wav);
  if (fr != FR_OK || clmt[0] < 2u || clmt[0] >= RESERVE_CLMT) return false;
  return reserve_write_trailer(bank, slot, clmt);
}

static uint32_t reserve_map[16][RESERVE_CLMT];
static bool reserve_map_ready;
static uint8_t reserve_fmt[16];
static bool reserve_fmt_ready;

static uint8_t reserve_fmt_file(unsigned slot) {
  char name[40];
  FIL wav;
  uint8_t h[44];
  UINT got = 0;
  snprintf(name, sizeof name, "bank%u/%u.0.wav", project_bank + 1u, slot);
  if (f_open(&wav, name, FA_READ) != FR_OK) return 0;
  bool read = f_read(&wav, h, 44, &got) == FR_OK && got == 44;
  f_close(&wav);
  if (!read || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return 0;
  uint16_t ch = (uint16_t)(h[22] | (h[23] << 8));
  uint32_t rate = (uint32_t)h[24] | ((uint32_t)h[25] << 8) | ((uint32_t)h[26] << 16) |
                  ((uint32_t)h[27] << 24);
  uint16_t bits = (uint16_t)(h[34] | (h[35] << 8));
  if (ch != 1 || rate != 44100u || bits != 16) return 0;
  return 2;
}

typedef struct {
  uint32_t lba;
  uint32_t sectors;
} SlotRun;
static SlotRun slot_run[16][31];
static uint8_t slot_run_n[16];
static bool slot_direct_ready;

static void slot_direct_build(void) {
  slot_direct_ready = false;
  sd_card_t *card = sd_get_by_num(0);
  if (!card || !card->state.mounted || !reserve_map_ready) return;
  FATFS *fs = &card->state.fatfs;
  if (fs->csize == 0 || fs->database == 0) return;
  for (uint8_t slot = 0; slot < 16; slot++) {
    const uint32_t *table = reserve_map[slot];
    uint32_t items = table[0];
    uint8_t n = 0;
    slot_run_n[slot] = 0;
    if (items < 3u || items > RESERVE_CLMT) continue;
    for (uint32_t i = 1; i + 1u < items && table[i] != 0 && n < 31; i += 2u) {
      uint32_t clusters = table[i];
      uint32_t cluster = table[i + 1u];
      if (cluster < 2u || clusters == 0) break;
      uint32_t lba = fs->database + (uint32_t)fs->csize * (cluster - 2u);
      uint32_t sectors = clusters * fs->csize;
      if (n && slot_run[slot][n - 1u].lba + slot_run[slot][n - 1u].sectors == lba)
        slot_run[slot][n - 1u].sectors += sectors;
      else {
        slot_run[slot][n].lba = lba;
        slot_run[slot][n].sectors = sectors;
        n++;
      }
    }
    slot_run_n[slot] = n;
  }
  slot_direct_ready = true;
}

static void reserve_fmt_load(void) {
  memset(reserve_fmt, 0, sizeof reserve_fmt);
  for (unsigned slot = 0; slot < 16; slot++) reserve_fmt[slot] = reserve_fmt_file(slot);
  reserve_fmt_ready = true;
  slot_direct_build();
}

bool sample_slot_direct_ready(void) { return slot_direct_ready; }

static bool slot_locate(uint8_t slot, uint32_t sector, uint32_t *lba, uint32_t *left) {
  uint32_t acc = 0;
  for (uint8_t i = 0; i < slot_run_n[slot]; i++) {
    uint32_t n = slot_run[slot][i].sectors;
    if (sector < acc + n) {
      *lba = slot_run[slot][i].lba + (sector - acc);
      *left = n - (sector - acc);
      return true;
    }
    acc += n;
  }
  return false;
}

bool sample_slot_direct_read(uint8_t slot, uint32_t offset, uint8_t *dst, uint32_t len,
                             uint32_t *got) {
  *got = 0;
  if (!slot_direct_ready || slot > 15 || !slot_run_n[slot] || !len) return false;
  uint32_t lba = 0, left = 0;
  uint32_t into = offset % 512u;
  if (!slot_locate(slot, offset / 512u, &lba, &left)) return false;
  enum { SLOT_READ_SECS = 8 };
  uint32_t secs = SLOT_READ_SECS;
  if (secs > left) secs = left;
  uint32_t need = (into + len + 511u) / 512u;
  if (secs > need) secs = need;
  if (!secs) return false;
  static uint8_t bounce[SLOT_READ_SECS * 512u];
  bool direct = into == 0 && len >= secs * 512u && (((uint32_t)dst & 3u) == 0);
  uint8_t *io = direct ? dst : bounce;
  if (disk_read(0, io, lba, (UINT)secs) != RES_OK) return false;
  uint32_t have = secs * 512u - into;
  if (have > len) have = len;
  if (!direct) memcpy(dst, bounce + into, have);
  *got = have;
  return true;
}

bool sample_slot_format_cached(uint8_t slot) {
  return reserve_fmt_ready && slot <= 15;
}

bool sample_slot_format(uint8_t slot, uint8_t *channels) {
  if (!sample_slot_format_cached(slot)) return false;
  if (reserve_fmt[slot] != 1 && reserve_fmt[slot] != 2) return false;
  *channels = reserve_fmt[slot];
  return true;
}

void sample_slot_format_set(uint8_t slot, uint8_t channels) {
  if (slot > 15) return;
  reserve_fmt[slot] = channels == 1 || channels == 2 ? channels : 0;
  reserve_fmt_ready = true;
}

static void reserve_map_store(unsigned slot, const uint32_t *clmt) {
  memcpy(reserve_map[slot], clmt, sizeof reserve_map[slot]);
}

void sample_slot_reserve_maps_load(void) {
  uint32_t clmt[RESERVE_CLMT];
  reserve_map_ready = false;
  reserve_fmt_ready = false;
  slot_direct_ready = false;
  if (!sample_slot_reserve_ok) return;
  for (unsigned slot = 0; slot < 16; slot++) {
    if (!reserve_build_map(project_bank, slot, clmt)) return;
    reserve_map_store(slot, clmt);
  }
  reserve_map_ready = true;
  reserve_fmt_load();
}

void sample_slot_reserve_check(void) {
  void sample_slot_copy_bar(uint8_t lit);
  uint32_t clmt[RESERVE_CLMT];
  FILINFO st;
  sample_slot_reserve_ok = false;
  reserve_map_ready = false;
  reserve_fmt_ready = false;
  if (f_stat("reserve/scratch.wav", &st) != FR_OK || st.fsize != RESERVE_SLOT_BYTES) return;
  for (unsigned bank = 0; bank < 16; bank++) {
    for (unsigned slot = 0; slot < 16; slot++) {
      if (!reserve_slot_sized(bank, slot)) return;
    }
  }
  unsigned built = 0;
  unsigned seen = 0;
  for (unsigned bank = 0; bank < 16; bank++) {
    for (unsigned slot = 0; slot < 16; slot++) {
      seen++;
      if (!reserve_slot_ok(bank, slot, clmt)) {
        if (!built) sample_slot_copy_bar(0);
        if (!reserve_build_map(bank, slot, clmt)) return;
        built++;
      }
      if (bank == project_bank) reserve_map_store(slot, clmt);
      if (built) {
        uint32_t lit = (uint32_t)(((uint64_t)seen * 16u) / 256u);
        if (lit > 16) lit = 16;
        sample_slot_copy_bar((uint8_t)lit);
      }
    }
  }
  sample_slot_reserve_ok = true;
  reserve_map_ready = true;
  reserve_fmt_load();
}

bool sample_slot_reserve_load(uint8_t slot, uint32_t *clmt) {
  if (!sample_slot_reserve_ok || slot > 15) return false;
  if (!reserve_map_ready) return reserve_slot_ok(project_bank, slot, clmt);
  memcpy(clmt, reserve_map[slot], sizeof reserve_map[slot]);
  return clmt[0] >= 3u && clmt[0] < RESERVE_CLMT && clmt[clmt[0] - 1u] == 0;
}

void usb_record_arm(uint8_t slot) {
  void sample_slot_reserve_deny(void);
  if (!sample_slot_reserve_ok) {
    sample_slot_reserve_deny();
    return;
  }
  if (rec_state != REC_IDLE || slot > 15 || !rec_cap) return;
  rec_slot = slot;
  rec_w = 0;
  rec_r = 0;
  rec_frames = 0;
  rec_written = 0;
  rec_overflow = 0;
  rec_peak = 0;
  rec_mix_peak = 0;
  rec_epoch_seen = rec_epoch;
  rec_block = 1;
  __dmb();
  rec_state = REC_ARMED;
}

bool usb_record_blocks_card(void) { return rec_block != 0; }

void usb_record_card_free(void) { rec_epoch++; }

int8_t usb_record_pad(void) {
  return rec_state == REC_IDLE ? -1 : (int8_t)rec_slot;
}

bool usb_record_started(void) {
  return rec_state == REC_CAPTURE || rec_state == REC_RECORD;
}

bool usb_record_claim_card(void) {
  if (rec_state != REC_IDLE) return false;
  sample_slot_voices_for_card();
  rec_block = 1;
  __dmb();
  return true;
}

void usb_record_release_card(void) { rec_block = 0; }

bool usb_record_claim_ready(void) {
  static absolute_time_t until, next;
  if (rec_state != REC_IDLE) return false;
  if (!rec_block) {
    if (!time_reached(next)) return false;
    rec_epoch_seen = rec_epoch;
    rec_block = 1;
    until = make_timeout_time_ms(100);
    return false;
  }
  if (rec_epoch != rec_epoch_seen) return true;
  if (time_reached(until)) {
    rec_block = 0;
    next = make_timeout_time_ms(500);
  }
  return false;
}

void usb_record_observe(int16_t left, int16_t right) {
  uint8_t state = rec_state;
  if (state == REC_ARMED) {
    if (rec_epoch == rec_epoch_seen) return;
    int32_t peak = left >= 0 ? left : -(int32_t)left;
    int32_t other = right >= 0 ? right : -(int32_t)right;
    if (other > peak) peak = other;
    if (peak < REC_THRESHOLD) return;
    rec_state = REC_CAPTURE;
    state = REC_CAPTURE;
  }
  if (state == REC_CAPTURE || state == REC_RECORD) rec_push(left, right);
}

static bool rec_write(const void *data, UINT bytes) {
  UINT got = 0;
  return f_write(&rec_file, data, bytes, &got) == FR_OK && got == bytes;
}

static bool rec_zeros(uint32_t bytes) {
  uint8_t buf[512];
  memset(buf, 0, sizeof buf);
  while (bytes) {
    UINT n = bytes > sizeof buf ? sizeof buf : bytes;
    if (!rec_write(buf, n)) return false;
    bytes -= n;
  }
  return true;
}

static bool rec_header(uint32_t data_bytes) {
  uint8_t h[44];
  uint32_t chunk = 36u + data_bytes;
  uint32_t rate = 44100;
  uint32_t bytes_sec = 88200u;
  memset(h, 0, sizeof h);
  memcpy(h, "RIFF", 4);
  h[4] = chunk;
  h[5] = chunk >> 8;
  h[6] = chunk >> 16;
  h[7] = chunk >> 24;
  memcpy(h + 8, "WAVEfmt ", 8);
  h[16] = 16;
  h[20] = 1;
  h[22] = 1;
  h[24] = rate;
  h[25] = rate >> 8;
  h[26] = rate >> 16;
  h[27] = rate >> 24;
  h[28] = bytes_sec;
  h[29] = bytes_sec >> 8;
  h[30] = bytes_sec >> 16;
  h[31] = bytes_sec >> 24;
  h[32] = 2;
  h[34] = 16;
  memcpy(h + 36, "data", 4);
  h[40] = data_bytes;
  h[41] = data_bytes >> 8;
  h[42] = data_bytes >> 16;
  h[43] = data_bytes >> 24;
  return f_lseek(&rec_file, 0) == FR_OK && rec_write(h, 44);
}

static void rec_catalogue(uint32_t audio_bytes) {
  if (!banks[project_bank]) return;
  int ord = metadata_ordinal(project_bank, rec_slot);
  if (ord >= 0) {
    SampleInfo *info = banks[project_bank]->sample[ord].snd[0];
    if (info) info->size = audio_bytes;
    return;
  }
  unsigned n = banks[project_bank]->num_samples;
  size_t offset = (n + 1) * sizeof(Sample);
  offset = (offset + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
  uint8_t *block = calloc(1, offset + (n + 1) * sizeof(SampleInfo));
  if (!block) return;
  Sample *samples = (Sample *)block;
  SampleInfo *infos = (SampleInfo *)(block + offset);
  for (unsigned i = 0; i < n; i++) {
    infos[i] = *banks[project_bank]->sample[i].snd[0];
    samples[i].snd[0] = &infos[i];
  }
  infos[n].size = audio_bytes;
  infos[n].bpm = 120;
  infos[n].num_channels = 0;
  infos[n].splice_trigger = 2;
  infos[n].slice_num = 1;
  infos[n].filename_index = rec_slot;
  samples[n].snd[0] = &infos[n];
  free(banks[project_bank]->sample);
  banks[project_bank]->sample = samples;
  banks[project_bank]->num_samples = n + 1;
  banks[project_bank]->valid_slots |= 1u << rec_slot;
}

static void sample_slot_reset_slot(uint8_t slot) {
  extern int8_t sample_slot_pitch[16];
  extern int8_t sample_slot_active_pitch[16];
  extern uint16_t sample_slot_attack[16];
  extern uint16_t sample_slot_decay[16];
  extern uint16_t sample_slot_release[16];
  extern uint8_t sample_slot_level[16];
  extern int8_t sample_slot_filter[16];
  extern uint8_t sample_slot_filter_q[16];
  extern bool sample_slot_slice_mode[16];
  extern bool sample_slot_slice_custom[16][16];
  extern bool sample_slot_slice_end_on[16][16];
  extern uint32_t sample_slot_slice_start[16][16];
  extern uint32_t sample_slot_slice_end[16][16];
  extern uint8_t sample_slot_slice_sel[16];
  extern bool sample_slot_region_custom[16];
  extern bool sample_slot_region_end_on[16];
  extern uint32_t sample_slot_region_start[16];
  extern uint32_t sample_slot_region_end[16];
  if (slot > 15) return;
  sample_slot_pitch[slot] = 0;
  sample_slot_active_pitch[slot] = 0;
  sample_slot_attack[slot] = 64;
  sample_slot_decay[slot] = 0;
  sample_slot_release[slot] = 64;
  sample_slot_level[slot] = 127;
  sample_slot_filter[slot] = 0;
  sample_slot_filter_q[slot] = 0;
  sample_slot_slice_mode[slot] = false;
  sample_slot_slice_sel[slot] = 0;
  sample_slot_region_custom[slot] = false;
  sample_slot_region_end_on[slot] = false;
  sample_slot_region_start[slot] = 0;
  sample_slot_region_end[slot] = 0;
  for (uint8_t slice = 0; slice < 16; slice++) {
    sample_slot_slice_custom[slot][slice] = false;
    sample_slot_slice_end_on[slot][slice] = false;
    sample_slot_slice_start[slot][slice] = 0;
    sample_slot_slice_end[slot][slice] = 0;
  }
}

static void rec_measure_peak(uint32_t bytes) {
  rec_mix_peak = 0;
  uint8_t buf[512];
  uint32_t done = 0;
  while (done < bytes) {
    uint32_t n = bytes - done;
    if (n > sizeof buf) n = sizeof buf;
    n &= ~1u;
    if (!n) return;
    UINT got = 0;
    if (f_lseek(&rec_file, RESERVE_DATA_OFF + done) != FR_OK ||
        f_read(&rec_file, buf, n, &got) != FR_OK || got != n)
      return;
    for (uint32_t i = 0; i + 1u < n; i += 2u) {
      int32_t s = (int16_t)(buf[i] | (buf[i + 1u] << 8));
      int32_t mag = s >= 0 ? s : -s;
      if (mag > rec_mix_peak) rec_mix_peak = mag;
    }
    done += n;
  }
}

static void rec_normalize(uint32_t frames) {
  if (rec_mix_peak < 1 || rec_mix_peak >= 32767 || !frames) return;
  uint8_t buf[512];
  uint32_t bytes = frames * 2u;
  uint32_t done = 0;
  while (done < bytes) {
    uint32_t n = bytes - done;
    if (n > sizeof buf) n = sizeof buf;
    n &= ~1u;
    if (!n) return;
    UINT got = 0;
    if (f_lseek(&rec_file, RESERVE_DATA_OFF + done) != FR_OK ||
        f_read(&rec_file, buf, n, &got) != FR_OK || got != n)
      return;
    for (uint32_t i = 0; i + 1u < n; i += 2u) {
      int32_t s = (int16_t)(buf[i] | (buf[i + 1u] << 8));
      s = s * 32767 / rec_mix_peak;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      buf[i] = (uint8_t)s;
      buf[i + 1u] = (uint8_t)((uint16_t)s >> 8);
    }
    if (f_lseek(&rec_file, RESERVE_DATA_OFF + done) != FR_OK || !rec_write(buf, n))
      return;
    done += n;
  }
}

static void rec_finish(bool keep) {
  rec_state = REC_FINISH;
  __dmb();
  uint32_t frames = rec_written;
  bool stored = rec_open && keep && frames;
  uint32_t audio = frames * 2u;
  if (rec_open) {
    if (stored) rec_normalize(frames);
    if (stored && rec_header(audio)) {
      sample_slot_format_set(rec_slot, 2);
      char name[32];
      f_sync(&rec_file);
      f_close(&rec_file);
      rec_open = false;
      snprintf(name, sizeof name, "bank%u/%u.0.wav.info", project_bank + 1, rec_slot);
      uint32_t saved_clmt[RESERVE_CLMT];
      bool had_map = reserve_slot_ok(project_bank, rec_slot, saved_clmt);
      if (f_open(&rec_file, name, FA_READ | FA_WRITE) == FR_OK) {
        uint8_t info[20];
        uint32_t flags = 120u;
        memset(info, 0, sizeof info);
        info[0] = audio;
        info[1] = audio >> 8;
        info[2] = audio >> 16;
        info[3] = audio >> 24;
        info[4] = flags;
        info[5] = flags >> 8;
        info[6] = flags >> 16;
        info[7] = flags >> 24;
        info[8] = 2;
        info[10] = 1;
        info[15] = audio;
        info[16] = audio >> 8;
        info[17] = audio >> 16;
        info[18] = audio >> 24;
        if (rec_write(info, 20) && had_map) {
          uint16_t items = (uint16_t)saved_clmt[0];
          uint8_t head[8] = {'C', 'L', (uint8_t)items, (uint8_t)(items >> 8),
                             (uint8_t)RESERVE_DATA_OFF, (uint8_t)(RESERVE_DATA_OFF >> 8),
                             (uint8_t)(RESERVE_DATA_OFF >> 16), (uint8_t)(RESERVE_DATA_OFF >> 24)};
          UINT wrote = 0;
          if (f_write(&rec_file, head, 8, &wrote) == FR_OK && wrote == 8 &&
              f_write(&rec_file, saved_clmt, items * 4u, &wrote) == FR_OK &&
              wrote == items * 4u) {
            f_truncate(&rec_file);
            f_sync(&rec_file);
          }
        }
        f_close(&rec_file);
        rec_catalogue(audio);
        sample_slot_reset_slot(rec_slot);
        {
          extern uint8_t sample_slot_edit;
          extern uint8_t sample_slot_active;
          extern uint8_t sample_slot_step_mask_pattern;
          sample_slot_edit = rec_slot;
          sample_slot_active = rec_slot;
          sample_slot_step_mask_pattern = 0xff;
        }
        project_mark();
      }
    } else {
      f_close(&rec_file);
    }
    rec_open = false;
  }
  memset(rec_samples, 0, sizeof rec_samples);
  rec_block = 0;
  rec_state = REC_IDLE;
}

bool sample_slot_resample_open(uint8_t slot) {
  if (slot > 15 || rec_state != REC_IDLE || rec_open || !sample_slot_reserve_ok)
    return false;
  char name[32];
  snprintf(name, sizeof name, "bank%u/%u.0.wav", project_bank + 1, slot);
  if (f_open(&rec_file, name, FA_READ | FA_WRITE) != FR_OK) return false;
  if (f_lseek(&rec_file, RESERVE_DATA_OFF) != FR_OK) {
    f_close(&rec_file);
    return false;
  }
  rec_slot = slot;
  rec_open = true;
  rec_written = 0;
  return true;
}

bool sample_slot_resample_write(const uint8_t *data, uint32_t bytes) {
  return rec_open && rec_write(data, bytes) && (rec_written += bytes, true);
}

bool sample_slot_resample_rewind(void) {
  rec_written = 0;
  return rec_open && f_lseek(&rec_file, RESERVE_DATA_OFF) == FR_OK;
}

bool sample_slot_resample_add(const uint8_t *data, uint32_t bytes) {
  if (!rec_open || !bytes) return false;
  uint8_t prev[512];
  uint8_t mixed[512];
  uint32_t at = RESERVE_DATA_OFF + rec_written;
  uint32_t done = 0;
  while (done < bytes) {
    uint32_t n = bytes - done;
    if (n > sizeof prev) n = sizeof prev;
    UINT got = 0;
    if (f_lseek(&rec_file, at) != FR_OK || f_read(&rec_file, prev, n, &got) != FR_OK ||
        got != n)
      return false;
    for (uint32_t i = 0; i + 1u < n; i += 2u) {
      int32_t a = (int16_t)(prev[i] | (prev[i + 1u] << 8));
      int32_t b = (int16_t)(data[done + i] | (data[done + i + 1u] << 8));
      int32_t s = a + b;
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      mixed[i] = (uint8_t)s;
      mixed[i + 1u] = (uint8_t)((uint16_t)s >> 8);
    }
    UINT wrote = 0;
    if (f_lseek(&rec_file, at) != FR_OK || f_write(&rec_file, mixed, n, &wrote) != FR_OK ||
        wrote != n)
      return false;
    at += n;
    done += n;
  }
  rec_written += bytes;
  return true;
}

void sample_slot_resample_commit(bool keep) {
  uint32_t frames = rec_written;
  if (rec_open && keep && frames) {
    rec_measure_peak(frames);
    rec_normalize(frames / 2u);
  }
  bool stored = rec_open && keep && frames && rec_header(frames);
  if (rec_open) {
    if (stored) {
      sample_slot_format_set(rec_slot, 2);
      char name[32];
      f_sync(&rec_file);
      f_close(&rec_file);
      rec_open = false;
      snprintf(name, sizeof name, "bank%u/%u.0.wav.info", project_bank + 1, rec_slot);
      uint32_t saved_clmt[RESERVE_CLMT];
      bool had_map = reserve_slot_ok(project_bank, rec_slot, saved_clmt);
      if (f_open(&rec_file, name, FA_READ | FA_WRITE) == FR_OK) {
        uint8_t info[20];
        uint32_t flags = 120u;
        uint32_t audio = frames;
        memset(info, 0, sizeof info);
        info[0] = audio;
        info[1] = audio >> 8;
        info[2] = audio >> 16;
        info[3] = audio >> 24;
        info[4] = flags;
        info[5] = flags >> 8;
        info[6] = flags >> 16;
        info[7] = flags >> 24;
        info[8] = 2;
        info[10] = 1;
        info[15] = audio;
        info[16] = audio >> 8;
        info[17] = audio >> 16;
        info[18] = audio >> 24;
        if (rec_write(info, 20) && had_map) {
          uint16_t items = (uint16_t)saved_clmt[0];
          uint8_t head[8] = {'C', 'L', (uint8_t)items, (uint8_t)(items >> 8),
                             (uint8_t)RESERVE_DATA_OFF, (uint8_t)(RESERVE_DATA_OFF >> 8),
                             (uint8_t)(RESERVE_DATA_OFF >> 16), (uint8_t)(RESERVE_DATA_OFF >> 24)};
          UINT wrote = 0;
          if (f_write(&rec_file, head, 8, &wrote) == FR_OK && wrote == 8 &&
              f_write(&rec_file, saved_clmt, items * 4u, &wrote) == FR_OK &&
              wrote == items * 4u) {
            f_truncate(&rec_file);
            f_sync(&rec_file);
          }
        }
        f_close(&rec_file);
        rec_catalogue(audio);
        sample_slot_reset_slot(rec_slot);
        {
          extern uint8_t sample_slot_edit;
          extern uint8_t sample_slot_active;
          extern uint8_t sample_slot_step_mask_pattern;
          sample_slot_edit = rec_slot;
          sample_slot_active = rec_slot;
          sample_slot_step_mask_pattern = 0xff;
        }
        project_mark();
      }
    } else {
      f_close(&rec_file);
    }
    rec_open = false;
  }
}

static bool rec_drain(void) {
  uint8_t buf[512];
  uint32_t end = rec_w;
  while (rec_r != end) {
    uint32_t n = 0;
    uint32_t at = rec_r;
    while (at != end && n + 2u <= sizeof buf) {
      int16_t left, right;
      rec_load(at, &left, &right);
      int32_t mixed = ((int32_t)left + right) / 2;
      if (mixed > 32767) mixed = 32767;
      if (mixed < -32768) mixed = -32768;
      int32_t mag = mixed >= 0 ? mixed : -mixed;
      if (mag > rec_mix_peak) rec_mix_peak = mag;
      int16_t s = (int16_t)mixed;
      buf[n] = (uint8_t)s;
      buf[n + 1u] = (uint8_t)((uint16_t)s >> 8);
      n += 2u;
      if (++at == rec_cap) at = 0;
    }
    if (n && !rec_write(buf, (UINT)n)) return false;
    rec_written += n / 2u;
    rec_r = at;
  }
  return true;
}

void usb_record_poll(bool d_held) {
  if (rec_state == REC_IDLE) return;
  if (!d_held && rec_state == REC_ARMED) {
    rec_block = 0;
    rec_state = REC_IDLE;
    return;
  }
  if (rec_state == REC_CAPTURE) {
    if (!d_held) {
      rec_finish(false);
      return;
    }
    if (rec_epoch == rec_epoch_seen) return;
    char name[32];
    snprintf(name, sizeof name, "bank%u/%u.0.wav", project_bank + 1, rec_slot);
    extern void audio_file_quiesce(void);
    extern volatile bool fil_is_open;
    extern char fil_current_name[32];
    if (fil_is_open && strcmp(fil_current_name, name) == 0) audio_file_quiesce();
    FRESULT opened = sample_slot_reserve_ok
                         ? f_open(&rec_file, name, FA_READ | FA_WRITE)
                         : FR_DENIED;
    if (opened == FR_LOCKED || opened == FR_TOO_MANY_OPEN_FILES) return;
    if (opened != FR_OK || f_lseek(&rec_file, RESERVE_DATA_OFF) != FR_OK) {
      if (opened == FR_OK) f_close(&rec_file);
      rec_finish(false);
      return;
    }
    rec_open = true;
    rec_state = REC_RECORD;
  }
  if (rec_state != REC_RECORD) return;
  bool capped = rec_frames >= REC_MAX_FRAMES;
  if (!rec_drain() || !d_held || rec_overflow || capped) {
    rec_state = REC_RECORD;
    rec_drain();
    rec_finish(true);
    if (capped) rec_full_until = time_us_32() + 180000u;
  }
}

bool usb_record_full_flash(void) {
  return rec_full_until && (int32_t)(rec_full_until - time_us_32()) > 0;
}

static void slot_path(char *name, uint8_t slot, bool info) {
  snprintf(name, 32, info ? "bank%u/%u.0.wav.info" : "bank%u/%u.0.wav",
           project_bank + 1, slot);
}

static void copy_bar(uint32_t done, uint32_t total) {
  void sample_slot_copy_bar(uint8_t lit);
  static uint8_t shown = 0xff;
  uint8_t lit = 16;
  if (total > 0) {
    uint32_t v = (uint32_t)(((uint64_t)done * 16u) / total);
    if (v > 16) v = 16;
    lit = (uint8_t)v;
  }
  if (lit == shown) return;
  shown = lit;
  sample_slot_copy_bar(lit);
}

static bool copy_path(const char *src, const char *dest, bool progress) {
  FIL in, out;
  if (f_open(&in, src, FA_READ) != FR_OK) return false;
  if (f_open(&out, dest, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    f_close(&in);
    return false;
  }
  uint8_t buf[512];
  bool ok = true;
  uint32_t done = 0;
  uint32_t total = (uint32_t)f_size(&in);
  if (progress) copy_bar(0, total);
  for (;;) {
    UINT got = 0;
    if (f_read(&in, buf, sizeof buf, &got) != FR_OK) {
      ok = false;
      break;
    }
    if (!got) break;
    UINT wrote = 0;
    if (f_write(&out, buf, got, &wrote) != FR_OK || wrote != got) {
      ok = false;
      break;
    }
    done += wrote;
    if (progress) copy_bar(done, total);
  }
  if (ok) f_sync(&out);
  f_close(&out);
  f_close(&in);
  return ok;
}

static void catalogue_remove(uint8_t slot) {
  int ord = metadata_ordinal(project_bank, slot);
  if (ord < 0 || !banks[project_bank]) return;
  unsigned n = banks[project_bank]->num_samples;
  banks[project_bank]->valid_slots &= ~(1u << slot);
  if (n == 1) {
    free(banks[project_bank]->sample);
    banks[project_bank]->sample = NULL;
    banks[project_bank]->num_samples = 0;
    return;
  }
  size_t offset = (n - 1) * sizeof(Sample);
  offset = (offset + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
  uint8_t *block = calloc(1, offset + (n - 1) * sizeof(SampleInfo));
  if (!block) return;
  Sample *samples = (Sample *)block;
  SampleInfo *infos = (SampleInfo *)(block + offset);
  unsigned w = 0;
  for (unsigned i = 0; i < n; i++) {
    if ((int)i == ord) continue;
    infos[w] = *banks[project_bank]->sample[i].snd[0];
    samples[w].snd[0] = &infos[w];
    w++;
  }
  free(banks[project_bank]->sample);
  banks[project_bank]->sample = samples;
  banks[project_bank]->num_samples = w;
}

static void catalogue_place(uint8_t src, uint8_t dest) {
  int from_ord = metadata_ordinal(project_bank, src);
  if (from_ord < 0 || !banks[project_bank]) return;
  SampleInfo from = *banks[project_bank]->sample[from_ord].snd[0];
  int dest_ord = metadata_ordinal(project_bank, dest);
  if (dest_ord >= 0) {
    SampleInfo *info = banks[project_bank]->sample[dest_ord].snd[0];
    info->size = from.size;
    info->num_channels = from.num_channels;
    info->bpm = from.bpm;
    info->one_shot = from.one_shot;
    info->splice_trigger = from.splice_trigger;
    info->slice_num = from.slice_num;
    return;
  }
  unsigned n = banks[project_bank]->num_samples;
  size_t offset = (n + 1) * sizeof(Sample);
  offset = (offset + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
  uint8_t *block = calloc(1, offset + (n + 1) * sizeof(SampleInfo));
  if (!block) return;
  Sample *samples = (Sample *)block;
  SampleInfo *infos = (SampleInfo *)(block + offset);
  for (unsigned i = 0; i < n; i++) {
    infos[i] = *banks[project_bank]->sample[i].snd[0];
    samples[i].snd[0] = &infos[i];
  }
  infos[n] = from;
  infos[n].slice_start = NULL;
  infos[n].slice_stop = NULL;
  infos[n].slice_type = NULL;
  infos[n].transients = NULL;
  infos[n].filename_index = dest;
  samples[n].snd[0] = &infos[n];
  free(banks[project_bank]->sample);
  banks[project_bank]->sample = samples;
  banks[project_bank]->num_samples = n + 1;
  banks[project_bank]->valid_slots |= 1u << dest;
}

static void sample_slot_copy_settings(uint8_t src, uint8_t dest) {
  extern int8_t sample_slot_pitch[16];
  extern int8_t sample_slot_active_pitch[16];
  extern uint16_t sample_slot_attack[16];
  extern uint16_t sample_slot_decay[16];
  extern uint16_t sample_slot_release[16];
  extern uint8_t sample_slot_level[16];
  extern int8_t sample_slot_filter[16];
  extern uint8_t sample_slot_filter_q[16];
  extern bool sample_slot_slice_mode[16];
  extern bool sample_slot_slice_custom[16][16];
  extern bool sample_slot_slice_end_on[16][16];
  extern uint32_t sample_slot_slice_start[16][16];
  extern uint32_t sample_slot_slice_end[16][16];
  extern uint8_t sample_slot_slice_sel[16];
  extern bool sample_slot_region_custom[16];
  extern bool sample_slot_region_end_on[16];
  extern uint32_t sample_slot_region_start[16];
  extern uint32_t sample_slot_region_end[16];
  if (src > 15 || dest > 15 || src == dest) return;
  sample_slot_pitch[dest] = sample_slot_pitch[src];
  sample_slot_active_pitch[dest] = sample_slot_active_pitch[src];
  sample_slot_attack[dest] = sample_slot_attack[src];
  sample_slot_decay[dest] = sample_slot_decay[src];
  sample_slot_release[dest] = sample_slot_release[src];
  sample_slot_level[dest] = sample_slot_level[src];
  sample_slot_filter[dest] = sample_slot_filter[src];
  sample_slot_filter_q[dest] = sample_slot_filter_q[src];
  sample_slot_slice_mode[dest] = sample_slot_slice_mode[src];
  sample_slot_slice_sel[dest] = sample_slot_slice_sel[src];
  sample_slot_region_custom[dest] = sample_slot_region_custom[src];
  sample_slot_region_end_on[dest] = sample_slot_region_end_on[src];
  sample_slot_region_start[dest] = sample_slot_region_start[src];
  sample_slot_region_end[dest] = sample_slot_region_end[src];
  for (uint8_t slice = 0; slice < 16; slice++) {
    sample_slot_slice_custom[dest][slice] = sample_slot_slice_custom[src][slice];
    sample_slot_slice_end_on[dest][slice] = sample_slot_slice_end_on[src][slice];
    sample_slot_slice_start[dest][slice] = sample_slot_slice_start[src][slice];
    sample_slot_slice_end[dest][slice] = sample_slot_slice_end[src][slice];
  }
}

static bool overwrite_reserved(const char *src, const char *dest) {
  FIL in, out;
  if (f_open(&in, src, FA_READ) != FR_OK) return false;
  if (f_open(&out, dest, FA_READ | FA_WRITE) != FR_OK) {
    f_close(&in);
    return false;
  }
  if (f_size(&in) != f_size(&out)) {
    f_close(&out);
    f_close(&in);
    return false;
  }
  uint8_t buf[512];
  bool ok = true;
  uint32_t done = 0;
  uint32_t total = (uint32_t)f_size(&in);
  copy_bar(0, total);
  for (;;) {
    UINT got = 0;
    if (f_read(&in, buf, sizeof buf, &got) != FR_OK) {
      ok = false;
      break;
    }
    if (!got) break;
    UINT wrote = 0;
    if (f_write(&out, buf, got, &wrote) != FR_OK || wrote != got) {
      ok = false;
      break;
    }
    done += wrote;
    copy_bar(done, total);
  }
  if (ok) f_sync(&out);
  f_close(&out);
  f_close(&in);
  return ok;
}

static bool paste_reserved_info(uint8_t src, uint8_t dest) {
  char from[40], to[40];
  FIL in, out;
  uint32_t clmt[RESERVE_CLMT];
  uint32_t src_len = 0;
  snprintf(from, sizeof from, "bank%u/%u.0.wav.info", project_bank + 1, src);
  snprintf(to, sizeof to, "bank%u/%u.0.wav.info", project_bank + 1, dest);
  if (!reserve_slot_ok(project_bank, dest, clmt)) return false;
  if (f_open(&in, from, FA_READ) != FR_OK) return false;
  if (!reserve_info_logical(&in, &src_len) || f_lseek(&in, 0) != FR_OK ||
      f_open(&out, to, FA_READ | FA_WRITE) != FR_OK) {
    f_close(&in);
    return false;
  }
  uint8_t buf[512];
  uint32_t left = src_len;
  bool ok = true;
  while (left && ok) {
    UINT want = left > sizeof buf ? (UINT)sizeof buf : (UINT)left;
    UINT got = 0, wrote = 0;
    if (f_read(&in, buf, want, &got) != FR_OK || got != want ||
        f_write(&out, buf, got, &wrote) != FR_OK || wrote != got)
      ok = false;
    else
      left -= got;
  }
  if (ok) {
    uint16_t items = (uint16_t)clmt[0];
    uint8_t head[8] = {'C', 'L', (uint8_t)items, (uint8_t)(items >> 8),
                       (uint8_t)RESERVE_DATA_OFF, (uint8_t)(RESERVE_DATA_OFF >> 8),
                       (uint8_t)(RESERVE_DATA_OFF >> 16), (uint8_t)(RESERVE_DATA_OFF >> 24)};
    UINT wrote = 0;
    ok = f_write(&out, head, 8, &wrote) == FR_OK && wrote == 8 &&
         f_write(&out, clmt, items * 4u, &wrote) == FR_OK && wrote == items * 4u;
    if (ok) ok = f_truncate(&out) == FR_OK && f_sync(&out) == FR_OK;
  }
  f_close(&out);
  f_close(&in);
  return ok;
}

bool sample_slot_transfer(uint8_t src, uint8_t dest) {
  if (src > 15 || dest > 15 || !usb_record_claim_card()) return false;
  char from[32], to[32];
  if (src == dest) {
    if (sample_slot_reserve_ok) {
      slot_path(from, src, true);
      uint8_t zero[4] = {0, 0, 0, 0};
      UINT wrote = 0;
      if (f_open(&rec_file, from, FA_WRITE) == FR_OK) {
        f_write(&rec_file, zero, 4, &wrote);
        f_close(&rec_file);
      }
    } else {
      slot_path(from, src, false);
      f_unlink(from);
      slot_path(from, src, true);
      f_unlink(from);
    }
    catalogue_remove(src);
  } else if (metadata_ordinal(project_bank, src) >= 0 && sample_slot_reserve_ok) {
    slot_path(from, src, false);
    slot_path(to, dest, false);
    bool wav = overwrite_reserved(from, to);
    bool info = wav && paste_reserved_info(src, dest);
    if (wav && info) {
      if (reserve_fmt_ready) reserve_fmt[dest] = reserve_fmt[src];
      catalogue_place(src, dest);
      sample_slot_copy_settings(src, dest);
    }
  } else if (metadata_ordinal(project_bank, src) >= 0) {
    slot_path(from, src, false);
    slot_path(to, dest, false);
    bool wav = copy_path(from, to, true);
    slot_path(from, src, true);
    slot_path(to, dest, true);
    bool info = copy_path(from, to, false);
    if (wav && info) {
      if (reserve_fmt_ready) reserve_fmt[dest] = reserve_fmt[src];
      catalogue_place(src, dest);
      sample_slot_copy_settings(src, dest);
    }
  }
  usb_record_release_card();
  project_mark();
  return true;
}
