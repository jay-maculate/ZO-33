#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <string.h>

#include "tusb.h"
#include "class/cdc/cdc_device.h"

// Fixed little-endian packet, 168 bytes, sent on the USB serial port.
// 0..1 'J''D', 2 version, 3 project (1..16), 4 slot, 5 flags, 6 pattern,
// 7 pitch, 8 cutoff, 9 resonance, 10 volume, 11 frame bytes,
// 12 usb level u16, 14..18 knob x/y/z u16,
// 20 size, 24 start, 28 end, 32 playhead (u32 byte offsets into the sample),
// 36 play slot, 37 step mask u16 for the selected sample on this pattern,
// 40 sixteen slice starts, 104 sixteen slice ends.
#define DISPLAY_BYTES 168
#define DISPLAY_PEAKS 200
#define DISPLAY_PEAK_CHUNK 40

static volatile uint8_t display_play_slot;
static volatile uint32_t display_playhead;

static inline void display_set_playhead(uint8_t slot, uint32_t phase) {
  display_play_slot = slot;
  display_playhead = phase;
}

static inline void put_display_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static inline void put_display_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static inline bool display_send(const uint8_t *bytes, uint16_t count) {
  if (tud_cdc_write_available() < count) return false;
  tud_cdc_write(bytes, count);
  tud_cdc_write_flush();
  return true;
}

static inline void display_wave_step(uint32_t size, uint32_t frame) {
  static FIL fil;
  static bool open;
  static bool failed;
  static uint8_t slot;
  static uint8_t bank;
  static uint32_t pos;
  static uint32_t span;
  static uint16_t peak_i;
  static int16_t peak_min;
  static int16_t peak_max;
  static int16_t ready[DISPLAY_PEAK_CHUNK * 2];
  static uint8_t ready_n;
  static uint16_t ready_first;

  if (!tud_cdc_connected() || sample_slot_edit != slot ||
      project_bank != bank) {
    if (open) {
      f_close(&fil);
      open = false;
    }
    slot = sample_slot_edit;
    bank = project_bank;
    pos = 0;
    peak_i = 0;
    ready_n = 0;
    failed = false;
    peak_min = 32767;
    peak_max = -32768;
  }
  if (usb_record_blocks_card() || sample_slot_voice_using_card()) {
    if (open) {
      f_close(&fil);
      open = false;
    }
    return;
  }
  if (ready_n && tud_cdc_connected()) {
    uint8_t packet[16 + DISPLAY_PEAK_CHUNK * 4];
    packet[0] = 'J';
    packet[1] = 'W';
    packet[2] = 1;
    packet[3] = slot;
    packet[4] = (uint8_t)(ready_first / DISPLAY_PEAK_CHUNK);
    packet[5] = (uint8_t)((DISPLAY_PEAKS + DISPLAY_PEAK_CHUNK - 1) / DISPLAY_PEAK_CHUNK);
    packet[6] = ready_n;
    packet[7] = 0;
    put_display_u32(packet + 8, size);
    put_display_u16(packet + 12, ready_first);
    put_display_u16(packet + 14, DISPLAY_PEAKS);
    for (uint8_t i = 0; i < ready_n; i++) {
      put_display_u16(packet + 16 + i * 4, (uint16_t)ready[i * 2]);
      put_display_u16(packet + 18 + i * 4, (uint16_t)ready[i * 2 + 1]);
    }
    if (!display_send(packet, (uint16_t)(16 + ready_n * 4))) return;
    ready_n = 0;
  }
  if (failed || !size || peak_i >= DISPLAY_PEAKS) return;
  if (!open) {
    int ord = sample_slot_ord(slot);
    if (ord < 0) {
      failed = true;
      return;
    }
    char name[32];
    format_sample_filename(name, bank, (uint8_t)ord, 0);
    if (f_open(&fil, name, FA_READ) != FR_OK) {
      failed = true;
      return;
    }
    if (f_lseek(&fil, WAV_HEADER + pos) != FR_OK) {
      f_close(&fil);
      failed = true;
      return;
    }
    open = true;
    span = size / DISPLAY_PEAKS;
    span -= span % frame;
    if (!span) span = frame;
  }

  uint8_t raw[1024];
  uint32_t room = sizeof raw;
  if (room > size - pos) room = size - pos;
  room -= room % frame;
  if (!room) {
    peak_i = DISPLAY_PEAKS;
    f_close(&fil);
    open = false;
    return;
  }
  UINT got = 0;
  if (f_read(&fil, raw, room, &got) != FR_OK || got < frame) {
    f_close(&fil);
    open = false;
    failed = true;
    return;
  }
  uint32_t frames = got / frame;
  for (uint32_t n = 0; n < frames * (frame / 2); n++) {
    int16_t s = (int16_t)(raw[n * 2] | (raw[n * 2 + 1] << 8));
    if (s < peak_min) peak_min = s;
    if (s > peak_max) peak_max = s;
  }
  pos += frames * frame;
  while (peak_i < DISPLAY_PEAKS &&
         (pos >= (uint32_t)(peak_i + 1) * span || pos >= size)) {
    ready[ready_n * 2] = peak_min;
    ready[ready_n * 2 + 1] = peak_max;
    ready_n++;
    if (ready_n == 1) ready_first = peak_i;
    peak_min = 32767;
    peak_max = -32768;
    peak_i++;
    if (ready_n == DISPLAY_PEAK_CHUNK || peak_i == DISPLAY_PEAKS || pos >= size) {
      uint8_t packet[16 + DISPLAY_PEAK_CHUNK * 4];
      packet[0] = 'J';
      packet[1] = 'W';
      packet[2] = 1;
      packet[3] = slot;
      packet[4] = (uint8_t)(ready_first / DISPLAY_PEAK_CHUNK);
      packet[5] = (uint8_t)((DISPLAY_PEAKS + DISPLAY_PEAK_CHUNK - 1) / DISPLAY_PEAK_CHUNK);
      packet[6] = ready_n;
      packet[7] = (uint8_t)frame;
      put_display_u32(packet + 8, size);
      put_display_u16(packet + 12, ready_first);
      put_display_u16(packet + 14, DISPLAY_PEAKS);
      for (uint8_t i = 0; i < ready_n; i++) {
        put_display_u16(packet + 16 + i * 4, (uint16_t)ready[i * 2]);
        put_display_u16(packet + 18 + i * 4, (uint16_t)ready[i * 2 + 1]);
      }
      if (!display_send(packet, (uint16_t)(16 + ready_n * 4))) return;
      ready_n = 0;
    }
    if (pos >= size) break;
  }
  if (pos >= size) {
    f_close(&fil);
    open = false;
    peak_i = DISPLAY_PEAKS;
  }
}

static inline void display_poll(int adc_x, int adc_y, int adc_z) {
  if (tud_cdc_available()) tud_cdc_read_flush();
  if (!tud_cdc_connected()) {
    display_wave_step(0, 2);
    return;
  }
  static uint32_t next_us;
  uint32_t now = time_us_32();
  if ((int32_t)(now - next_us) < 0) {
    uint32_t size = 0, frame = 2;
    sample_slot_playable(sample_slot_edit, &size, &frame);
    display_wave_step(size, frame);
    return;
  }
  next_us = now + 50000;

  uint8_t packet[DISPLAY_BYTES];
  memset(packet, 0, sizeof packet);
  packet[0] = 'J';
  packet[1] = 'D';
  packet[2] = 1;
  packet[3] = (uint8_t)(project_bank + 1);
  packet[4] = sample_slot_edit;
  packet[5] = (sample_slot_slice_mode[sample_slot_edit] ? 1u : 0u) |
              (sample_slot_playing ? 2u : 0u);
  packet[6] = sample_slot_pattern_sel;
  packet[7] = (uint8_t)sample_slot_pitch[sample_slot_edit];
  packet[8] = (uint8_t)sample_slot_filter[sample_slot_edit];
  packet[9] = sample_slot_filter_q[sample_slot_edit];
  packet[10] = (uint8_t)project_vol;
  uint32_t size = 0, frame = 2, start = 0, end = 0;
  if (sample_slot_playable(sample_slot_edit, &size, &frame)) {
    uint8_t slice = sample_slot_slice_sel[sample_slot_edit];
    sample_slot_region(sample_slot_edit, slice,
                       sample_slot_slice_mode[sample_slot_edit], &start, &end);
    for (uint8_t i = 0; i < 16; i++) {
      uint32_t ss = 0, se = 0;
      sample_slot_region(sample_slot_edit, i, true, &ss, &se);
      put_display_u32(packet + 40 + i * 4, ss);
      put_display_u32(packet + 104 + i * 4, se);
    }
  }
  packet[11] = (uint8_t)frame;
  put_display_u16(packet + 12, usb_audio_level);
  put_display_u16(packet + 14, (uint16_t)adc_x);
  put_display_u16(packet + 16, (uint16_t)adc_y);
  put_display_u16(packet + 18, (uint16_t)adc_z);
  put_display_u32(packet + 20, size);
  put_display_u32(packet + 24, start);
  put_display_u32(packet + 28, end);
  put_display_u32(packet + 32, display_playhead);
  packet[36] = display_play_slot;
  uint16_t steps = 0;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample_slot_edit &&
        n->step < 16)
      steps |= (uint16_t)(1u << n->step);
  }
  put_display_u16(packet + 37, steps);
  display_send(packet, DISPLAY_BYTES);
  display_wave_step(size, frame);
}

#endif
