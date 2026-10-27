#ifndef LIB_MIDI_CALLBACK_H
#define LIB_MIDI_CALLBACK_H 1

// #define DEBUG_MIDI 1
#define MIDI_MAX_NOTES 128
#define MIDI_MAX_TIME_ON 10000  // 10 seconds
#define MIDI_RESET_EVERY_BEAT 16
#define MIDI_CLOCK_MULTIPLIER 2

uint32_t note_hit[MIDI_MAX_NOTES];
int midi_bpm_detect[7];
uint8_t midi_bpm_detect_i = 0;
bool note_on[MIDI_MAX_NOTES];
uint32_t midi_last_time = 0;
uint32_t midi_delta_sum = 0;
uint32_t midi_delta_count = 0;
#define MIDI_DELTA_COUNT_MAX 32
uint32_t midi_timing_count = 0;
const uint8_t midi_timing_modulus = 24;

#include "midi_note_key.h"

void midi_start() {
  midi_timing_count = 24 * MIDI_RESET_EVERY_BEAT - 1;
  cancel_repeating_timer(&timer);
  do_restart_playback = true;
  timer_step();
#ifdef INCLUDE_ZEPTOCORE
  update_repeating_timer_to_bpm(project_bpm);
#else
  update_repeating_timer_to_bpm(sf->bpm_tempo);
#endif
  button_mute = false;
  trigger_button_mute = false;
}
void midi_continue() { midi_start(); }
void midi_stop() {
  midi_timing_count = 24 * MIDI_RESET_EVERY_BEAT - 1;
  trigger_button_mute = true;
  do_stop_playback = true;
}

void midi_control_change (uint8_t channel, uint8_t control, uint8_t value) {
  // printf("chan/cc/value: %d/%d/%d\n", channel, control, value);
  uint8_t new_adcvalue = (value * 2) ;
  switch (control) {
#if defined(SEEK_TEST_CONTROLS) && SEEK_TEST_CONTROLS
  case 113:
    set_audio_variant(value);
    break;
  case 114:
    // Exercise the ordinary acknowledged save/reopen path, only in an empty
    // preset slot. Never replace an existing preset through this test control.
    if(value<16&&!savefile_has_data[value]) {
      uint8_t previous=savefile_current;
      savefile_current=value;
      savefile_do_save();
      savefile_current=previous;
    }
    break;
  case 115:
    if(value<16&&savefile_has_data[value]) {
      uint8_t previous=savefile_current;
      savefile_current=value;
      savefile_do_load();
      savefile_current=previous;
    }
    break;
#endif
  case cc_volume: { // volume
      uint8_t new_vol = new_adcvalue; // is it 256 total?
#ifdef INCLUDE_ZEPTOCORE
      if (new_vol != project_vol) project_vol = new_vol;
#else
      if (new_vol != sf->vol) {
        sf->vol = new_vol;
      }
#endif
      break;
  }
  case cc_tempo: { // tempo
      uint8_t new_bpm = new_adcvalue ; // what is range?
#ifdef INCLUDE_ZEPTOCORE
      if (new_bpm != project_bpm) project_set_bpm(new_bpm);
#else
      if (new_bpm != sf->bpm_tempo) {
        sf->bpm_tempo = new_bpm;
        // printf("sf-vol: %d\n", sf->vol);
      }
#endif
      break;
  }
  default:
    return;
  }
}
// Comparator function for qsort
int compare_ints(const void *a, const void *b) {
  return (*(int *)a - *(int *)b);
}

// Function to find the median of an array with 7 elements
int findMedian(int arr[], uint8_t size) {
  // Create a copy of the array to avoid modifying the original
  int arrCopy[size];
  memcpy(arrCopy, arr, sizeof(int) * size);

  // Sort the copy of the array
  qsort(arrCopy, size, sizeof(int), compare_ints);

  // Return the middle element from the sorted copy

  return arrCopy[size / 2];
}

void midi_timing() {
  midi_timing_count++;
  if (midi_timing_count % (24 * MIDI_RESET_EVERY_BEAT) == 0) {
    // reset
    clock_in_beat_total = -1;
    clock_in_beat_last = -1;
  }
  if (midi_timing_count % (midi_timing_modulus / MIDI_CLOCK_MULTIPLIER) == 0) {
    // soft sync
    clock_in_do_update();
  }
  uint32_t now_time = time_us_32();
  if (midi_last_time > 0) {
    midi_delta_sum += now_time - midi_last_time;
    midi_delta_count++;
    if (midi_delta_count == MIDI_DELTA_COUNT_MAX) {
      midi_bpm_detect[midi_bpm_detect_i] =
          (int)round(1250000.0 * MIDI_CLOCK_MULTIPLIER * MIDI_DELTA_COUNT_MAX /
                     (float)(midi_delta_sum));
      midi_bpm_detect_i++;
      if (midi_bpm_detect_i == 7) {
        midi_bpm_detect_i = 0;
      }
      // sort midi_bpm_detect

      int bpm_new = findMedian(midi_bpm_detect, 7);
#ifdef INCLUDE_ZEPTOCORE
      if (bpm_new > 60 && bpm_new < 260 && bpm_new != project_bpm)
        project_set_bpm(bpm_new);
#else
      if (bpm_new > 60 && bpm_new < 260 && bpm_new != sf->bpm_tempo) {
        sf->bpm_tempo = bpm_new;
      }
#endif
      //   if (bpm_input - 7 != bpm_set) {
      //     // set bpm
      //   }
      midi_delta_count = 0;
      midi_delta_sum = 0;
    }
  }
  midi_last_time = now_time;
}
#endif
