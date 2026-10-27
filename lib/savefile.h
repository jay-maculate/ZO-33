// Copyright 2023-2025 Zack Scholl, GPLv3.0

#ifndef LIB_SAVEFILE
#define LIB_SAVEFILE 1

#include "sequencer.h"
#include "utils.h"
#include <stddef.h>

typedef struct SaveFile {
  uint32_t vol : 9;
  uint32_t bpm_tempo : 9;
  uint32_t bank : 7;
  uint32_t sample : 7;
  Sequencer *sequencers[3][16];
  uint8_t sequence_sel[3];
  bool fx_active[16];
  uint8_t fx_param[16][3];
  uint64_t stay_in_sync : 1;
  uint64_t pitch_val_index : 7;
  uint64_t do_retrig_pitch_changes : 1;
  uint64_t do_retrig_volume_ramps : 1;
  uint64_t feature_magic : 8;
  uint64_t _padding : 46;
} SaveFile;

#define SAVEFILE_PATHNAME "save.bin"
#define SAVEFILE_FEATURE_MAGIC 0xA5
void SaveFile_sanitize(SaveFile *sf) {
  if (sf->feature_magic != SAVEFILE_FEATURE_MAGIC) {
    sf->do_retrig_volume_ramps = 1;
    sf->feature_magic = SAVEFILE_FEATURE_MAGIC;
  }
  for (uint8_t i = 0; i < 16; i++) {
    sf->fx_active[i] = false;
    sf->fx_param[i][0] = 0;
    sf->fx_param[i][1] = 0;
    sf->fx_param[i][2] = 0;
  }
}
SaveFile *SaveFile_malloc() {
  SaveFile *sf;
  // Each Sequencer owns a separate allocation below. The previous parent
  // allocation also reserved space for 48 unused Sequencers.
  sf = malloc(sizeof(SaveFile));
  sf->bank = 0;
  sf->sample = 0;
  sf->vol = 120;
  sf->bpm_tempo = 90;
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 16; j++) {
      sf->sequencers[i][j] = NULL;
    }
  }
  sf->sequence_sel[0] = 0;
  sf->sequence_sel[1] = 0;
  sf->sequence_sel[2] = 0;

  for (uint8_t i = 0; i < 16; i++) {
    sf->fx_active[i] = false;
    sf->fx_param[i][0] = 0;
    sf->fx_param[i][1] = 0;
    sf->fx_param[i][2] = 0;
  }
  sf->do_retrig_pitch_changes = 1;
  sf->do_retrig_volume_ramps = 1;
  sf->feature_magic = SAVEFILE_FEATURE_MAGIC;
  sf->_padding = 0;
  sf->stay_in_sync = 1;  // default to staying in sync
  sf->pitch_val_index = 48;
  return sf;
}

void SaveFile_free(SaveFile *sf) {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 16; j++) {
      if (sf->sequencers[i][j]) Sequencer_free(sf->sequencers[i][j]);
    }
  }
  free(sf);
}

#ifdef NOSDCARD
bool SaveFile_load(SaveFile *sf, uint8_t savefile_index) {
  char fname[32];
  format_prefixed_int32(fname, "savefile", savefile_index);
  // load from the file on the file system
  FILE *file = fopen(fname, "rb");
  if (file == NULL) {
    return false;
  }
  fread(sf, sizeof(SaveFile), 1, file);
  SaveFile_sanitize(sf);

  // read sequencers
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 16; j++) {
      fread(sf->sequencers[i][j], sizeof(Sequencer), 1, file);
    }
  }
  fclose(file);
  return true;
}
#endif
#ifndef NOSDCARD

bool SaveFile_load(SaveFile *sf, uint8_t savefile_index) {
  FIL fil = {0};
  char fname[32];
  format_prefixed_int32(fname, "savefile", savefile_index);
  if (f_open(&fil, fname, FA_READ) != FR_OK) return false;
  // Keep the existing on-card layout, but never use saved heap/code pointers.
  // Allocation addresses can change between compatible firmware builds.
  SaveFile saved;
  UINT bytes_read;
  bool loaded = f_size(&fil) >= sizeof(SaveFile) &&
      f_read(&fil, &saved, sizeof saved, &bytes_read) == FR_OK &&
      bytes_read == sizeof saved;
  loaded = f_close(&fil) == FR_OK && loaded;
  if (loaded) {
    memcpy(sf, &saved, offsetof(SaveFile, sequencers));
    size_t tail = offsetof(SaveFile, sequence_sel);
    memcpy((char *)sf + tail, (char *)&saved + tail, sizeof saved - tail);
    SaveFile_sanitize(sf);
  }
  return loaded;
}

bool SaveFile_save(SaveFile *sf, uint8_t savefile_index) {
  FRESULT fr;
  FIL file; /* File object */
  char fname[32];

  format_prefixed_int32(fname, "savefile", savefile_index);
  fr = f_open(&file, fname, FA_WRITE | FA_CREATE_ALWAYS);
  if (FR_OK != fr) {
    return false;
  }
  unsigned int bw;
  SaveFile_sanitize(sf);
  f_write(&file, sf, sizeof(SaveFile), &bw);
  f_close(&file);
  return true;
}

#endif

#endif
