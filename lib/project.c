#include "project.h"
#include "bank_metadata.h"
#include "ff.h"
#include "usb_record.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

volatile bool project_dirty;
uint8_t project_bank;
bool project_mode;
bool project_confirm;

extern bool sample_slot_playing;

#define PROJECT_MAGIC_V1 0x3150524au
#define PROJECT_MAGIC 0x3250524au

typedef struct SampleSlotNote {
  uint8_t pattern;
  uint8_t sample;
  uint8_t step;
  uint8_t slice;
  uint8_t nudge;
  uint8_t velocity;
  uint8_t offset;
  int8_t pitch;
  int8_t filter;
  uint8_t filter_q;
} SampleSlotNote;

extern SampleSlotNote sample_slot_notes[1024];
extern uint16_t sample_slot_note_count;
extern uint16_t sample_slot_pattern_mask;
extern uint16_t sample_slot_step_mask;
extern uint8_t sample_slot_step_mask_pattern;
extern int8_t sample_slot_pitch[16];
extern int8_t sample_slot_active_pitch[16];
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
extern int8_t sample_slot_filter[16];
extern uint8_t sample_slot_filter_q[16];
extern uint16_t sample_slot_attack[16];
extern uint16_t sample_slot_decay[16];
extern uint16_t sample_slot_release[16];
extern uint8_t sample_slot_level[16];
extern uint8_t sample_slot_edit;
extern uint8_t sample_slot_pattern_sel;
extern uint8_t sample_slot_pattern_next;
extern uint8_t sample_slot_chain[64];
extern uint8_t sample_slot_chain_len;
extern uint8_t sample_slot_chain_pos;
extern bool sample_slot_chain_pending;
extern volatile bool sample_slot_sd_busy;

uint16_t project_get_bpm(void);
void project_set_bpm(uint16_t bpm);
uint16_t project_get_vol(void);
void project_set_vol(uint16_t vol);
void update_repeating_timer_to_bpm(float bpm);

static bool project_io(FIL *file, void *data, UINT bytes, bool write) {
  UINT got = 0;
  if (write) return f_write(file, data, bytes, &got) == FR_OK && got == bytes;
  return f_read(file, data, bytes, &got) == FR_OK && got == bytes;
}

static void project_clamp(uint8_t slot) {
  int ord = metadata_ordinal(project_bank, slot);
  if (ord < 0 || !banks[project_bank]) return;
  SampleInfo *info = banks[project_bank]->sample[ord].snd[0];
  if (!info || !info->size) return;
  uint32_t size = info->size;
  if (sample_slot_region_custom[slot] && sample_slot_region_start[slot] > size)
    sample_slot_region_start[slot] = 0;
  if (sample_slot_region_end_on[slot] && sample_slot_region_end[slot] > size)
    sample_slot_region_end[slot] = size;
  for (uint8_t slice = 0; slice < 16; slice++) {
    if (sample_slot_slice_custom[slot][slice] &&
        sample_slot_slice_start[slot][slice] > size)
      sample_slot_slice_start[slot][slice] = 0;
    if (sample_slot_slice_end_on[slot][slice] &&
        sample_slot_slice_end[slot][slice] > size)
      sample_slot_slice_end[slot][slice] = size;
  }
}

static bool project_bounds(FIL *file, bool write, bool ends) {
  if (!project_io(file, sample_slot_slice_custom, sizeof sample_slot_slice_custom, write))
    return false;
  if (ends && !project_io(file, sample_slot_slice_end_on,
                          sizeof sample_slot_slice_end_on, write))
    return false;
  if (!project_io(file, sample_slot_slice_start, sizeof sample_slot_slice_start, write))
    return false;
  if (!project_io(file, sample_slot_slice_end, sizeof sample_slot_slice_end, write))
    return false;
  if (!project_io(file, sample_slot_region_start, sizeof sample_slot_region_start, write))
    return false;
  if (!project_io(file, sample_slot_region_end, sizeof sample_slot_region_end, write))
    return false;
  if (ends && !project_io(file, sample_slot_region_end_on,
                          sizeof sample_slot_region_end_on, write))
    return false;
  return true;
}

static bool project_body(FIL *file, bool write, bool ends) {
  uint32_t magic = PROJECT_MAGIC;
  uint16_t notes = sample_slot_note_count;
  uint16_t bpm = project_get_bpm();
  uint8_t edit = sample_slot_edit;
  uint8_t pattern = sample_slot_pattern_sel;
  uint8_t chain = sample_slot_chain_len;
  if (!project_io(file, &magic, 4, write) ||
      magic != (ends ? PROJECT_MAGIC : PROJECT_MAGIC_V1))
    return false;
  if (!project_io(file, &notes, 2, write) || notes > 1024) return false;
  if (!project_io(file, &bpm, 2, write)) return false;
  if (!project_io(file, &edit, 1, write) || !project_io(file, &pattern, 1, write) ||
      !project_io(file, &chain, 1, write))
    return false;
  if (!write) {
    sample_slot_note_count = notes;
    project_set_bpm(bpm);
    if (edit < 16) sample_slot_edit = edit;
    if (pattern < 16) sample_slot_pattern_sel = pattern;
    sample_slot_chain_len = chain > 64 ? 64 : chain;
  }
  if (!project_io(file, sample_slot_chain, 64, write)) return false;
  if (!project_io(file, sample_slot_pitch, 16, write)) return false;
  if (!project_io(file, sample_slot_active_pitch, 16, write)) return false;
  if (!project_io(file, sample_slot_slice_mode, 16, write)) return false;
  if (!project_io(file, sample_slot_slice_sel, 16, write)) return false;
  if (!project_io(file, sample_slot_region_custom, 16, write)) return false;
  if (!project_io(file, sample_slot_filter, 16, write)) return false;
  if (!project_io(file, sample_slot_filter_q, 16, write)) return false;
  if (!project_bounds(file, write, ends)) return false;
  if (!project_io(file, sample_slot_notes, notes * sizeof(SampleSlotNote), write))
    return false;
  if (write) {
    uint16_t vol = project_get_vol();
    return project_io(file, sample_slot_attack, sizeof sample_slot_attack, true) &&
           project_io(file, sample_slot_decay, sizeof sample_slot_decay, true) &&
           project_io(file, sample_slot_release, sizeof sample_slot_release, true) &&
           project_io(file, sample_slot_level, sizeof sample_slot_level, true) &&
           project_io(file, &vol, 2, true);
  }
  UINT got = 0;
  if (f_read(file, sample_slot_attack, sizeof sample_slot_attack, &got) != FR_OK ||
      got != sizeof sample_slot_attack) {
    for (uint8_t i = 0; i < 16; i++) sample_slot_attack[i] = 64;
  }
  if (f_read(file, sample_slot_decay, sizeof sample_slot_decay, &got) != FR_OK ||
      got != sizeof sample_slot_decay) {
    memset(sample_slot_decay, 0, sizeof sample_slot_decay);
  }
  if (f_read(file, sample_slot_release, sizeof sample_slot_release, &got) != FR_OK ||
      got != sizeof sample_slot_release) {
    for (uint8_t i = 0; i < 16; i++) sample_slot_release[i] = 64;
  }
  if (f_read(file, sample_slot_level, sizeof sample_slot_level, &got) != FR_OK ||
      got != sizeof sample_slot_level) {
    memset(sample_slot_level, 127, sizeof sample_slot_level);
  }
  uint16_t vol = 180;
  if (f_read(file, &vol, 2, &got) == FR_OK && got == 2) project_set_vol(vol);
  return true;
}

static void project_path(char *out, size_t n, const char *leaf) {
  snprintf(out, n, "bank%u/%s", project_bank + 1u, leaf);
}

static bool project_save_held(void) {
  char dir[8], neu[24], bin[24];
  snprintf(dir, sizeof dir, "bank%u", project_bank + 1u);
  f_mkdir(dir);
  project_path(neu, sizeof neu, "project.new");
  project_path(bin, sizeof bin, "project.bin");
  FIL file;
  bool ok = f_open(&file, neu, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK &&
            project_body(&file, true, true);
  if (ok) ok = f_sync(&file) == FR_OK;
  f_close(&file);
  if (ok) {
    f_unlink(bin);
    ok = f_rename(neu, bin) == FR_OK;
  }
  if (ok) project_dirty = false;
  return ok;
}

static void project_remember(void) {
  FIL file;
  if (f_open(&file, "project.sel", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
  UINT wrote = 0;
  f_write(&file, &project_bank, 1, &wrote);
  f_sync(&file);
  f_close(&file);
}

static void project_pattern_mask(void) {
  uint16_t mask = 0;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    uint8_t pattern = sample_slot_notes[i].pattern;
    if (pattern < 16) mask |= (uint16_t)(1u << pattern);
  }
  sample_slot_pattern_mask = mask;
  sample_slot_step_mask_pattern = sample_slot_pattern_sel;
  mask = 0;
  if (sample_slot_pattern_sel > 15 ||
      !(sample_slot_pattern_mask & (uint16_t)(1u << sample_slot_pattern_sel))) {
    sample_slot_step_mask = 0;
    return;
  }
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample_slot_edit &&
        n->step < 16)
      mask |= (uint16_t)(1u << n->step);
  }
  sample_slot_step_mask = mask;
}

static void project_blank(void) {
  sample_slot_playing = false;
  sample_slot_note_count = 0;
  sample_slot_pattern_mask = 0;
  sample_slot_step_mask = 0;
  sample_slot_chain_len = 0;
  sample_slot_chain_pos = 0;
  sample_slot_chain_pending = false;
  sample_slot_pattern_sel = 0;
  sample_slot_pattern_next = 0;
  sample_slot_edit = 0;
  memset(sample_slot_chain, 0, sizeof sample_slot_chain);
  memset(sample_slot_pitch, 0, sizeof sample_slot_pitch);
  memset(sample_slot_active_pitch, 0, sizeof sample_slot_active_pitch);
  memset(sample_slot_slice_mode, 0, sizeof sample_slot_slice_mode);
  memset(sample_slot_slice_sel, 0, sizeof sample_slot_slice_sel);
  memset(sample_slot_region_custom, 0, sizeof sample_slot_region_custom);
  memset(sample_slot_region_end_on, 0, sizeof sample_slot_region_end_on);
  memset(sample_slot_filter, 0, sizeof sample_slot_filter);
  memset(sample_slot_filter_q, 0, sizeof sample_slot_filter_q);
  memset(sample_slot_slice_custom, 0, sizeof sample_slot_slice_custom);
  memset(sample_slot_slice_end_on, 0, sizeof sample_slot_slice_end_on);
  memset(sample_slot_slice_start, 0, sizeof sample_slot_slice_start);
  memset(sample_slot_slice_end, 0, sizeof sample_slot_slice_end);
  memset(sample_slot_region_start, 0, sizeof sample_slot_region_start);
  memset(sample_slot_region_end, 0, sizeof sample_slot_region_end);
  for (uint8_t i = 0; i < 16; i++) {
    sample_slot_attack[i] = 64;
    sample_slot_decay[i] = 0;
    sample_slot_release[i] = 64;
    sample_slot_level[i] = 127;
  }
  project_set_vol(180);
  project_set_bpm(90);
  project_dirty = false;
}

static void project_chain_home(void) {
  if (sample_slot_chain_len == 0 || sample_slot_chain[0] >= 16) return;
  sample_slot_pattern_sel = sample_slot_chain[0];
  sample_slot_pattern_next = sample_slot_chain[0];
  sample_slot_chain_pos = 0;
}

void project_boot(void) {
  FIL file;
  UINT got = 0;
  uint8_t slot = 0;
  if (f_open(&file, "project.sel", FA_READ) == FR_OK) {
    if (f_read(&file, &slot, 1, &got) == FR_OK && got == 1 && slot < 16)
      project_bank = slot;
    f_close(&file);
  }
  project_load();
}

void project_load(void) {
  FIL file;
  char path[24];
  project_path(path, sizeof path, "project.bin");
  if (f_open(&file, path, FA_READ) != FR_OK) return;
  uint32_t magic = 0;
  UINT got = 0;
  bool ok = f_read(&file, &magic, 4, &got) == FR_OK && got == 4;
  if (ok) ok = f_lseek(&file, 0) == FR_OK;
  if (ok && magic == PROJECT_MAGIC) ok = project_body(&file, false, true);
  else if (ok && magic == PROJECT_MAGIC_V1) {
    ok = project_body(&file, false, false);
    memset(sample_slot_slice_end_on, 0, sizeof sample_slot_slice_end_on);
    memset(sample_slot_region_end_on, 0, sizeof sample_slot_region_end_on);
  } else {
    ok = false;
  }
  f_close(&file);
  if (!ok) return;
  project_chain_home();
  project_pattern_mask();
  for (uint8_t slot = 0; slot < 16; slot++) project_clamp(slot);
  project_dirty = false;
}

void project_poll(void) {
  if (!project_dirty) return;
  if (sample_slot_sd_busy && !usb_record_blocks_card()) return;
  if (!usb_record_claim_ready()) return;
  project_save_held();
  usb_record_release_card();
}

extern void audio_file_quiesce(void);

bool project_select(uint8_t bank) {
  if (bank > 15 || bank == project_bank) return false;
  sample_slot_playing = false;
  if (!usb_record_claim_card()) return false;
  if (project_dirty) project_save_held();
  char dir[8];
  snprintf(dir, sizeof dir, "bank%u", bank + 1u);
  f_mkdir(dir);
  audio_file_quiesce();
  if (!metadata_bank_reload(bank)) {
    usb_record_release_card();
    return false;
  }
  project_bank = bank;
  sample_slot_reserve_maps_load();
  project_blank();
  project_load();
  update_repeating_timer_to_bpm(project_get_bpm());
  project_remember();
  usb_record_release_card();
  return true;
}

bool project_clear(void) {
  sample_slot_playing = false;
  if (!usb_record_claim_card()) return false;
  char path[32];
  uint8_t zero[4] = {0, 0, 0, 0};
  for (uint8_t slot = 0; slot < 16; slot++) {
    snprintf(path, sizeof path, "bank%u/%u.0.wav.info", project_bank + 1u, slot);
    FIL info;
    UINT wrote = 0;
    if (f_open(&info, path, FA_WRITE) == FR_OK) {
      f_write(&info, zero, 4, &wrote);
      f_close(&info);
    }
  }
  project_path(path, sizeof path, "project.bin");
  f_unlink(path);
  audio_file_quiesce();
  if (metadata_bank_reload(project_bank)) {
    project_blank();
    update_repeating_timer_to_bpm(project_get_bpm());
  }
  usb_record_release_card();
  return true;
}

static uint16_t project_used_mask;

bool project_bank_used(uint8_t bank) {
  return bank < 16 && (project_used_mask & (uint16_t)(1u << bank)) != 0;
}

void project_select_prepare(void) {
  project_used_mask = 0;
  if (sample_slot_note_count > 0 || sample_slot_chain_len > 0 ||
      (banks[project_bank] && banks[project_bank]->num_samples > 0))
    project_used_mask |= (uint16_t)(1u << project_bank);
  if (!usb_record_claim_card()) return;
  for (uint8_t bank = 0; bank < 16; bank++) {
    if (bank == project_bank) continue;
    if (banks[bank] && banks[bank]->num_samples > 0) {
      project_used_mask |= (uint16_t)(1u << bank);
      continue;
    }
    char path[24];
    FIL file;
    UINT got = 0;
    uint8_t head[11];
    snprintf(path, sizeof path, "bank%u/project.bin", bank + 1u);
    if (f_open(&file, path, FA_READ) != FR_OK) continue;
    bool ok = f_read(&file, head, 11, &got) == FR_OK && got == 11;
    f_close(&file);
    if (!ok) continue;
    uint16_t notes = (uint16_t)(head[4] | (head[5] << 8));
    if (notes > 0 || head[10] > 0)
      project_used_mask |= (uint16_t)(1u << bank);
  }
  usb_record_release_card();
}
