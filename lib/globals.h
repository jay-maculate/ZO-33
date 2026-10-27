// Copyright 2023-2025 Zack Scholl, GPLv3.0

#ifndef GLOBALS_LIB
#define GLOBALS_LIB 1

bool repeating_timer_callback(struct repeating_timer *t);
bool timer_step();
struct repeating_timer timer;
// audio_pool.h
audio_buffer_pool_t *ap;

clock_t time_of_initialization;
FIL fil_current;
char fil_current_name[32];
volatile bool fil_is_open;
uint32_t last_seeked = 1;
uint8_t cpu_utilization;
volatile int32_t phases[2];
volatile int32_t phases_old[2];
volatile int32_t phase_new;
uint16_t mem_index[2];
bool mem_use;
volatile bool phase_change;
unsigned int fil_bytes_read;
unsigned int fil_bytes_read2;
// uint16_t sf->bpm_tempo = 185;
uint16_t bpm_last = 185;
uint16_t global_start_tempo = 0;
volatile uint8_t sel_sample_cur = 0;
volatile uint8_t sel_sample_next = 0;
volatile uint8_t sel_bank_cur = 0;
volatile uint8_t sel_bank_next = 0;
uint8_t sel_bank_select = 0;
bool fil_current_change = false;

uint8_t banks_with_samples[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t banks_with_samples_num = 0;
uint8_t audio_variant = 0;
uint8_t audio_variant_num = 0;
bool clock_input_present_first = false;
bool fil_current_change_force = false;

void set_audio_variant(uint8_t x) {
  if (x <= audio_variant_num && x != audio_variant) {
    audio_variant = x;
    fil_current_change_force = true;
  }
}

FRESULT fil_result;

volatile bool phase_forward = 1;
volatile bool sync_using_sdcard = false;

uint16_t total_number_samples = 0;

// one wire midi
volatile bool use_onewiremidi = false;

bool do_switch_between_clock_and_midi = false;
bool playback_was_stopped_clock = false;

SequencerHandler sequencerhandler[3];

// voice 1 + 2
// voice 1 is always an envelope UP
// voice 2 is always an envelope DOWN
// voice 1 is only voice that jumps
// voice 2 takes place of old voice and continues
EnvelopeLinearInteger *envelope_filter;
Noise *noise_wobble;
Noise *noise_feedback;
Noise *noise_duration;
uint vols[2];

float vol3 = 0;
float envelope_pitch_val;
float envelope_wobble_val;
int32_t beat_current = 0;
int32_t beat_current_show = 0;
int32_t beat_total = 0;
uint debounce_quantize = 0;
uint16_t debounce_file_change = 0;
int64_t bpm_timer_counter = 0;
int64_t bpm_timer_counter_last = 0;
uint8_t retrig_beat_num = 0;
uint16_t retrig_timer_reset = 96;
bool retrig_first = false;
bool retrig_ready = false;
volatile float retrig_vol = 1.0;
float retrig_vol_step = 0;
volatile uint8_t retrig_pitch = 48;
int8_t retrig_pitch_change = 0;
int16_t retrig_filter_change = 0;
int16_t retrig_filter_original = 0;
int32_t scratch_lfo_inc = 0;
int32_t scratch_lfo_val = 0;
float scratch_lfo_hz = 0.7;
#define SCRATCH_LFO_1_HZ 9340

bool only_play_kicks = false;
bool only_play_snares = false;
int8_t do_layer_kicks = -1;

// random things
uint16_t probability_of_random_jump = 0;
uint16_t probability_of_random_retrig = 0;
uint16_t probability_of_random_tunnel = 0;
uint8_t tunneling_original_sample = 0;
uint8_t tunneling_is_on = 0;
uint8_t random_sequence_length = 0;
uint8_t random_sequence_arr[64];
void regenerate_random_sequence_arr() {
  uint8_t sequence_probability = random_integer_in_range(1, 50);
  for (uint8_t i = 0; i < 64; i++) {
    random_sequence_arr[i] = random_integer_in_range(0, 64);
    if (random_integer_in_range(1, 100) < sequence_probability && i < 60 &&
        random_sequence_arr[i] < 60) {
      random_sequence_arr[i + 1] = random_sequence_arr[i] + 1;
      random_sequence_arr[i + 2] = random_sequence_arr[i] + 2;
      i += 2;
    }
  }
}

// buttons
// mode toggles
//   mode  ==0  ==1
uint8_t mode_buttons16 = 0;
#ifdef INCLUDE_ZEPTOCORE
#define JUMP_PAGE_LOCK_DEFAULT 0
#define JUMP_PAGE_LOCK_FIRST 1
#define JUMP_PAGE_LOCK_SECOND 2
#define JUMP_PAGE_LOCK_THIRD 3
uint8_t jump_page_lock = JUMP_PAGE_LOCK_DEFAULT;
#endif
bool mode_mute = 0;
bool mode_play = 0;
bool mute_because_of_playback_type = false;
bool mode_hands_on_unmute = false;
uint32_t mode_held_duration = 0;
uint32_t mode_held_start_time = 0;
uint8_t mode_amiga_index = 0;
uint32_t mode_digital_saturation = 0;
uint8_t mode_digital_bass = 0;
uint8_t mode_digital_smear = 0;
uint8_t mode_digital_jitter = 0;
uint8_t mode_chaos_trembler = 0;
bool dual_leds_holding_mode = false;
bool dual_leds_holding_tap = false;
#define MODE_HOLD_DURATION_THRESHOLD 300

bool key3_activated = false;
int8_t single_key_on = -1;
uint8_t key3_pressed_keys[3] = {0, 0, 0};

#ifndef INCLUDE_ZEPTOCORE
SaveFile *sf;
uint8_t savefile_current = 0;
bool savefile_has_data[16];
#endif
uint16_t project_bpm = 90;
uint16_t project_vol = 180;
#ifdef INCLUDE_ZEPTOCORE
LEDS *leds;
LEDText *ledtext;
#endif

#ifdef INCLUDE_MIDI
#define MIDIOUTS 6
MidiOut *midiout[6];
bool midi_input_activated = false;
#endif

// setup some constants for CCs
const uint8_t cc_knobx = 12;
const uint8_t cc_knoby = 13;
const uint8_t cc_knobz = 14;
const uint8_t cc_tempo = 15;
const uint8_t cc_pitch = 16;
const uint8_t cc_volume = 7;
const uint8_t cc_pan = 10;
const uint8_t cc_randsequence = 18;
const uint8_t cc_djfilter = 19;
const uint8_t cc_realtime_stretch = 20;
const uint8_t cc_sampleselect = 21;
const uint8_t cc_randtunnel = 22;
const uint8_t cc_quantize = 23;
const uint8_t cc_randjump = 24;
const uint8_t cc_randfx = 25;
const uint8_t cc_randfxbank = 26;

bool toggle_chain_play = false;
bool toggle_chain_rec = false;

int16_t dub_step_break = -1;
uint16_t dub_step_divider = 0;
uint8_t dub_step_beat = 0;

// add variable to keep track of variation
volatile uint8_t sel_variation = 0;
volatile int8_t sel_variation_next = 0;
bool sel_variation_fadeout = false;
uint8_t global_brightness = 50;  // 0 - 100
bool global_amen_cv_bipolar = true;
#define AMEN_CV_BEHAVIOR_JUMP 0
#define AMEN_CV_BEHAVIOR_REPEAT 1
#define AMEN_CV_BEHAVIOR_SPLIT 2
uint8_t global_amen_cv_behavior = AMEN_CV_BEHAVIOR_JUMP;
bool global_break_cv_bipolar = true;
bool global_sample_cv_bipolar = true;
volatile bool clock_start_stop_sync = false;
bool clock_output_trig = false;
int8_t cv_reset_override = -1;
bool cv_reset_override_active = false;
bool clock_behavior_sync_slice = false;
uint32_t clock_output_trig_time = 0;
bool clock_out_do = false;
bool clock_out_ready = false;
uint32_t ecto_trig_out_last = 0;
volatile bool clock_in_do = false;
bool clock_input_absent_zeptocore = false;
bool clock_in_ready = false;
uint8_t clock_in_activator = 0;
int32_t clock_in_beat_total = 0;
int32_t clock_in_beat_last = 0;
bool should_skip_clock_pulse = false;
volatile uint32_t clock_in_diff_2x = 0;
volatile uint32_t clock_in_last_time = 0;
uint32_t clock_in_last_last_time = 0;
uint8_t led_text_time = 200;

#include "clock_stop.h"

typedef struct SampleSelection {
  uint8_t bank : 4;    // 0-15
  uint8_t sample : 4;  // 0-15
} SampleSelection;

bool global_knobx_sample_selector = false;
SampleSelection *sample_selection;
uint8_t sample_selection_num = 0;

uint8_t do_update_beat_repeat = 0;
uint8_t beat_start_retrig = 0;
bool do_retrig_at_end_of_phrase = false;

bool do_stop_playback = false;
bool do_restart_playback = true;
bool playback_stopped = true;
bool playback_restarted = false;
volatile bool audio_callback_in_mute = false;
float latency_factor = 5.6f;
bool do_calibration_mode = false;
bool mode_amiga = false;
bool mode_toggle_momentary = false;
bool mute_soft = false;

#define DEBOUNCE_UINT8_LED_BAR 0
#define DEBOUNCE_UINT8_LED_SPIRAL1 1
#define DEBOUNCE_UINT8_LED_WALL 2
#define DEBOUNCE_UINT8_LED_DIAGONAL 3
#define DEBOUNCE_UINT8_LED_RANDOM1 4
#define DEBOUNCE_UINT8_LED_RANDOM2 5
#define DEBOUNCE_UINT8_LED_TRIANGLE 6
#define DEBOUNCE_UINT8_NUM 7
DebounceUint8 *debouncer_uint8[DEBOUNCE_UINT8_NUM];
#ifdef INCLUDE_ZEPTOCORE
DebounceDigits *debouncer_digits;
#endif

bool sdcard_startup_is_starting = false;
volatile bool button_mute = false;
bool trigger_button_mute = false;

// reverb

// lfos
int32_t lfo_pan_val = 0;
// TODO: make the lfo pan step adjustable?
int32_t lfo_pan_step = Q16_16_2PI / (96 * 5);
int32_t lfo_tremelo_val = 0;
// TODO: make the lfo tremelo step adjustable?
int32_t lfo_tremelo_step = Q16_16_2PI / (96);

#define ENVELOPE_PITCH_THRESHOLD 0.01

uint16_t global_filter_index = resonantfilter_fc_max;
uint8_t global_filter_lphp = 0;

#ifdef INCLUDE_ZEPTOCORE
// Runtime stereo balance (not saved). Center keeps both channels at unity so
// default playback matches the previous unpanned output.
int32_t global_pan_left = Q16_16_1;
int32_t global_pan_right = Q16_16_1;

static inline void set_global_pan(uint32_t value, uint32_t value_max) {
  if (value_max == 0) {
    value_max = 1;
  }
  if (value > value_max) {
    value = value_max;
  }

  if (value * 2 <= value_max) {
    global_pan_left = Q16_16_1;
    global_pan_right =
        (int32_t)(((uint64_t)value * 2u * (uint32_t)Q16_16_1) / value_max);
  } else {
    global_pan_right = Q16_16_1;
    global_pan_left = (int32_t)(((uint64_t)(value_max - value) * 2u *
                                 (uint32_t)Q16_16_1) /
                                value_max);
  }
}

#endif
// pitches derived from supercollider
/*
a=(Tuning.et(24).ratios/4)++(Tuning.et(24).ratios/2)++Tuning.et(24).ratios++[2];
a.do({ arg v;
        v.postln;
});
a.size
*/

// probability_max_values dictates how many out of every note will be activated
bool clock_did_activate = false;
bool beat_did_activate = false;
const uint8_t probability_max_values[16] = {
    // 0 = never
    // 1 = 1/64
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 24, 32, 48,
};
const uint8_t probability_max_values_off[16] = {
    // 0 = never
    // 1 = 1/64
    0, 48, 32, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4,
};
#define PITCH_VAL_MAX 73
#define PITCH_VAL_MID 48
float pitch_vals[PITCH_VAL_MAX] = {
    0.25,
    0.25732555916084,
    0.26486577358976,
    0.27262693316622,
    0.28061551207721,
    0.2888381742179,
    0.29730177875047,
    0.30601338582592,
    0.31498026247343,
    0.32420988866242,
    0.33370996354212,
    0.34348841186409,
    0.35355339059278,
    0.36391329571,
    0.37457676921856,
    0.38555270635132,
    0.39685026299132,
    0.40847886330947,
    0.42044820762598,
    0.43276828050212,
    0.44544935906914,
    0.45850202160122,
    0.47193715633965,
    0.48576597057551,
    0.5,
    0.51465111832169,
    0.52973154717953,
    0.54525386633244,
    0.56123102415443,
    0.5776763484358,
    0.59460355750095,
    0.61202677165183,
    0.62996052494685,
    0.64841977732483,
    0.66741992708425,
    0.68697682372817,
    0.70710678118557,
    0.72782659142,
    0.74915353843713,
    0.77110541270263,
    0.79370052598263,
    0.81695772661895,
    0.84089641525197,
    0.86553656100424,
    0.89089871813828,
    0.91700404320245,
    0.94387431267929,
    0.97153194115102,
    1.0,
    1.0293022366434,
    1.0594630943591,
    1.0905077326649,
    1.1224620483089,
    1.1553526968716,
    1.1892071150019,
    1.2240535433037,
    1.2599210498937,
    1.2968395546497,
    1.3348398541685,
    1.3739536474563,
    1.4142135623711,
    1.45565318284,
    1.4983070768743,
    1.5422108254053,
    1.5874010519653,
    1.6339154532379,
    1.6817928305039,
    1.7310731220085,
    1.7817974362766,
    1.8340080864049,
    1.8877486253586,
    1.943063882302,
    2,
};

// Planned retrig - a controllable stutter effect that transitions
// from specified start values to current values
float planned_retrig_start_vol = 1.0;
float planned_retrig_stop_vol = 1.0;
uint8_t planned_retrig_start_pitch = PITCH_VAL_MID;
uint8_t planned_retrig_stop_pitch = PITCH_VAL_MID;
uint8_t planned_retrig_beat_num = 0;
uint16_t planned_retrig_timer_reset = 96;
uint8_t planned_retrig_rate_divisor = 1;
bool planned_retrig_ready = false;
bool planned_retrig_first = false;
bool fuzz_auto_active = false;
bool fuzz_manual_lock = false;
int16_t planned_retrig_filter_start = 0;
int16_t planned_retrig_filter_stop = 0;
int16_t planned_retrig_filter_restore = 0;
int16_t planned_retrig_filter_change = 0;
bool planned_retrig_filter_active = false;
// Current interpolated values during playback
volatile float planned_retrig_vol = 1.0;
volatile uint8_t planned_retrig_pitch = PITCH_VAL_MID;
float planned_retrig_vol_step = 0;
int8_t planned_retrig_pitch_step = 0;
// Probability multiplier (0-100): 100 = base probability
uint8_t planned_retrig_probability = 0;

#define PLANNED_RETRIG_USE_CURRENT_VOL (-1.0f)
#define PLANNED_RETRIG_USE_CURRENT_PITCH 127


#define MODE_JUMP 0
#define MODE_MASH 1
#define MODE_BASS 2
#define MODE_SAMPLE_SLOT 3

// One sixteenth note of the calibrated beat timer (15e6/314441 ticks).
#define SAMPLE_SLOT_TICKS_PER_STEP 48
typedef struct SampleSlotVoice {
  int8_t play_pitch;
  int8_t play_filter;
  float play_filter_hz;
  bool play_filter_glide;
  uint8_t play_filter_q;
  int8_t applied_filter;
  uint8_t applied_filter_q;
  bool filter_reset;
  bool filter_dirty;
  ResonantFilter filter_l;
  ResonantFilter filter_r;
  uint8_t sample;
  uint8_t play_velocity;
  uint8_t pending_velocity;
  uint32_t started;
  bool restart;
  bool cue;
  bool kill;
  bool open;
  bool seek;
  bool primed;
  bool have_ahead;
  bool short_read;
  uint8_t file_sample;
  FIL fil;
  uint32_t phase;
  uint32_t play_end;
  uint16_t attack_pos;
  uint16_t attack_len;
  uint16_t decay_pos;
  uint16_t decay_len;
  uint16_t release_len;
  uint16_t release_left;
  uint16_t frac_q16;
  uint16_t hold_run;
  uint16_t resume_fade;
  uint32_t pending_phase;
  uint32_t pending_end;
  uint8_t pending_sample;
  bool steal_after;
  bool defer_restart;
  int16_t hold_l;
  int16_t hold_r;
  int16_t ahead_l;
  int16_t ahead_r;
  bool reserve;
  bool pref_eof;
  uint8_t src_channels;
  uint32_t data_off;
  uint32_t src_bytes;
  uint32_t file_at;
  uint32_t pref_at;
  volatile bool pref_drop;
  uint32_t card_after;
  uint32_t next_phase;
  uint16_t next_len;
  bool next_on;
  bool next_need;
  volatile uint16_t pref_r;
  volatile uint16_t pref_w;
  uint32_t clmt[64];
} SampleSlotVoice;

#define SAMPLE_SLOT_VOICES 4
SampleSlotVoice sample_slot_voice[SAMPLE_SLOT_VOICES];

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

#define SAMPLE_SLOT_NOTES 1024
SampleSlotNote sample_slot_notes[SAMPLE_SLOT_NOTES];
uint16_t sample_slot_note_count = 0;
int8_t sample_slot_pitch[16];
int8_t sample_slot_active_pitch[16];
uint16_t sample_slot_attack[16];
uint16_t sample_slot_decay[16];
uint16_t sample_slot_release[16];
uint8_t sample_slot_level[16];
#define SAMPLE_SLOT_ENV_MAX 22050u
#define SAMPLE_SLOT_ENV_MIN 64u
#define SAMPLE_SLOT_XFADE 176u
bool sample_slot_slice_mode[16];
bool sample_slot_slice_custom[16][16];
bool sample_slot_slice_end_on[16][16];
uint32_t sample_slot_slice_start[16][16];
uint32_t sample_slot_slice_end[16][16];
uint8_t sample_slot_slice_sel[16];
bool sample_slot_region_custom[16];
bool sample_slot_region_end_on[16];
uint32_t sample_slot_region_start[16];
uint32_t sample_slot_region_end[16];
volatile bool sample_slot_sd_busy;

uint16_t project_get_bpm(void) {
  return project_bpm ? project_bpm : 90;
}

void project_set_bpm(uint16_t bpm) {
  if (bpm >= 30 && bpm <= 300) project_bpm = bpm;
}

uint16_t project_get_vol(void) { return project_vol; }

void project_set_vol(uint16_t vol) {
  if (vol <= 511) project_vol = vol;
}
static const int8_t sample_slot_pad_semitone[16] = {
    4, 5, 6, 7, 0, 1, 2, 3, -4, -3, -2, -1, -8, -7, -6, -5};
int8_t sample_slot_filter[16];
uint8_t sample_slot_filter_q[16];
uint8_t sample_slot_pattern_sel = 0;
uint8_t sample_slot_pattern_next = 0;
uint8_t sample_slot_chain[64];
uint8_t sample_slot_chain_len = 0;
uint8_t sample_slot_chain_pos = 0;
bool sample_slot_chain_entered = false;
bool sample_slot_chain_pending = false;
bool sample_slot_chain_restart = false;
uint8_t sample_slot_chain_edit[64];
uint8_t sample_slot_chain_edit_len = 0;
volatile bool sample_slot_chain_publish = false;
volatile bool sample_slot_chain_hold_save = false;

static inline bool sample_slot_voice_using_card(void) {
  if (sample_slot_sd_busy) return true;
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++)
    if (sample_slot_voice[i].open || sample_slot_voice[i].restart) return true;
  return false;
}

uint16_t sample_slot_pattern_mask = 0;
uint16_t sample_slot_step_mask = 0;
uint8_t sample_slot_step_mask_pattern = 0xff;

static inline void sample_slot_pattern_mask_refresh(void) {
  uint16_t mask = 0;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    uint8_t pattern = sample_slot_notes[i].pattern;
    if (pattern < 16) mask |= (uint16_t)(1u << pattern);
  }
  sample_slot_pattern_mask = mask;
}

static inline bool sample_slot_pattern_stored(uint8_t index) {
  return index < 16 && (sample_slot_pattern_mask & (uint16_t)(1u << index));
}

uint8_t sample_slot_edit = 0;
uint32_t sample_slot_led_flash_until[16];
uint8_t sample_slot_active = 0;

static inline void sample_slot_step_mask_refresh(void) {
  sample_slot_step_mask_pattern = sample_slot_pattern_sel;
  if (sample_slot_pattern_sel > 15 ||
      !(sample_slot_pattern_mask & (uint16_t)(1u << sample_slot_pattern_sel))) {
    sample_slot_step_mask = 0;
    return;
  }
  uint16_t mask = 0;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample_slot_edit &&
        n->step < 16)
      mask |= (uint16_t)(1u << n->step);
  }
  sample_slot_step_mask = mask;
}
bool sample_slot_playing = false;
bool sample_slot_resample_pick = false;
volatile bool sample_slot_resample_busy = false;
uint8_t sample_slot_resample_solo = 0xff;
void sample_slot_resample_run(uint8_t slot);
bool sample_slot_hold_edited = false;
bool sample_slot_note_mode = false;
uint8_t sample_slot_ticks = 0;
volatile uint16_t sample_slot_due = 0;
uint8_t sample_slot_step = 0;
uint32_t sample_slot_stamp = 0;

#include "project.h"

static inline int sample_slot_ord(uint8_t slot) {
  if (!banks[project_bank] || slot > 15) return -1;
  for (unsigned s = 0; s < banks[project_bank]->num_samples; s++)
    if (metadata_filename_index(project_bank, s) == slot) return (int)s;
  return -1;
}

static inline bool sample_slot_sample_has_notes(uint8_t sample) {
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample) return true;
  }
  return false;
}

static inline SampleSlotNote *sample_slot_note_at(uint8_t sample, uint8_t step) {
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample &&
        n->step == step)
      return n;
  }
  return NULL;
}

static inline void sample_slot_note_remove(uint16_t index) {
  if (index >= sample_slot_note_count) return;
  sample_slot_notes[index] = sample_slot_notes[sample_slot_note_count - 1];
  sample_slot_note_count--;
}

static inline void sample_slot_pattern_clear(uint8_t pattern) {
  for (uint16_t i = 0; i < sample_slot_note_count;) {
    if (sample_slot_notes[i].pattern == pattern)
      sample_slot_note_remove(i);
    else
      i++;
  }
  sample_slot_pattern_mask_refresh();
  sample_slot_step_mask_refresh();
}

static inline void sample_slot_pattern_copy(uint8_t dest) {
  uint8_t src = sample_slot_pattern_sel;
  if (dest > 15) return;
  if (dest == src) {
    sample_slot_pattern_clear(src);
    project_mark();
    return;
  }
  uint16_t src_n = 0, dest_n = 0;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    if (sample_slot_notes[i].pattern == src) src_n++;
    else if (sample_slot_notes[i].pattern == dest) dest_n++;
  }
  if ((uint32_t)sample_slot_note_count - dest_n + src_n > SAMPLE_SLOT_NOTES)
    return;
  sample_slot_pattern_clear(dest);
  uint16_t end = sample_slot_note_count;
  for (uint16_t i = 0; i < end; i++) {
    if (sample_slot_notes[i].pattern != src) continue;
    sample_slot_notes[sample_slot_note_count] = sample_slot_notes[i];
    sample_slot_notes[sample_slot_note_count].pattern = dest;
    sample_slot_note_count++;
  }
  sample_slot_pattern_sel = dest;
  sample_slot_pattern_next = dest;
  sample_slot_chain_edit_len = 0;
  sample_slot_chain_publish = false;
  if (sample_slot_chain_len > 0) {
    if (sample_slot_playing && sample_slot_chain_pos < sample_slot_chain_len)
      sample_slot_chain[sample_slot_chain_pos] = dest;
    else
      sample_slot_chain[0] = dest;
  }
  sample_slot_pattern_mask_refresh();
  sample_slot_step_mask_refresh();
  project_mark();
}

static inline bool sample_slot_live_note(uint8_t pad) {
  if (!sample_slot_playing || pad > 15) return false;
  uint8_t sample = sample_slot_edit;
  uint8_t step = sample_slot_step;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample &&
        n->step == step)
      return false;
  }
  if (sample_slot_note_count >= SAMPLE_SLOT_NOTES) return false;
  SampleSlotNote *n = &sample_slot_notes[sample_slot_note_count++];
  n->pattern = sample_slot_pattern_sel;
  n->sample = sample;
  n->step = step;
  n->nudge = 0;
  n->velocity = sample_slot_level[sample] ? sample_slot_level[sample] : 127;
  n->offset = 0;
  n->filter = sample_slot_filter[sample];
  n->filter_q = sample_slot_filter_q[sample];
  if (sample_slot_slice_mode[sample]) {
    n->slice = pad;
    n->pitch = sample_slot_active_pitch[sample];
  } else {
    int pitch = (int)sample_slot_pitch[sample] + sample_slot_pad_semitone[pad];
    if (pitch > 12) pitch = 12;
    if (pitch < -12) pitch = -12;
    n->slice = 0;
    n->pitch = (int8_t)pitch;
  }
  sample_slot_pattern_mask_refresh();
  sample_slot_step_mask_refresh();
  project_mark();
  return true;
}

static inline bool sample_slot_note_toggle(uint8_t sample, uint8_t step) {
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern == sample_slot_pattern_sel && n->sample == sample &&
        n->step == step) {
      sample_slot_note_remove(i);
      sample_slot_pattern_mask_refresh();
  sample_slot_step_mask_refresh();
      project_mark();
      return true;
    }
  }
  if (sample_slot_note_count >= SAMPLE_SLOT_NOTES) return false;
  SampleSlotNote *n = &sample_slot_notes[sample_slot_note_count++];
  n->pattern = sample_slot_pattern_sel;
  n->sample = sample;
  n->step = step;
  n->slice = sample_slot_slice_mode[sample] ? sample_slot_slice_sel[sample] : 0;
  n->nudge = 0;
  n->velocity = sample_slot_level[sample] ? sample_slot_level[sample] : 127;
  n->offset = 0;
  n->pitch = sample_slot_active_pitch[sample];
  n->filter = sample_slot_filter[sample];
  n->filter_q = sample_slot_filter_q[sample];
  sample_slot_pattern_mask_refresh();
  sample_slot_step_mask_refresh();
  project_mark();
  return true;
}

static inline int sample_slot_claim_cued(uint8_t slot, uint32_t phase) {
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    SampleSlotVoice *v = &sample_slot_voice[i];
    if (v->sample == slot && v->cue && v->phase == phase) return i;
  }
  return -1;
}

static inline int sample_slot_claim(uint8_t slot) {
  if (sample_slot_ord(slot) < 0) return -1;
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    SampleSlotVoice *v = &sample_slot_voice[i];
    if (v->sample == slot &&
        (v->open || v->restart || (v->fil.obj.fs && v->file_sample == slot)))
      return i;
  }
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    if (!sample_slot_voice[i].open && !sample_slot_voice[i].restart) return i;
  }
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    SampleSlotVoice *v = &sample_slot_voice[i];
    if (v->sample == slot && v->cue) return i;
  }
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < SAMPLE_SLOT_VOICES; i++)
    if (sample_slot_voice[i].started < sample_slot_voice[oldest].started)
      oldest = i;
  return oldest;
}

static inline void sample_slot_trigger_voice(uint8_t voice, uint8_t slot,
                                             uint32_t phase) {
  if (voice >= SAMPLE_SLOT_VOICES || sample_slot_ord(slot) < 0) return;
  SampleSlotVoice *v = &sample_slot_voice[voice];
  bool crowded = true;
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    if (i == voice) continue;
    SampleSlotVoice *o = &sample_slot_voice[i];
    if (!o->open && !o->restart && !o->cue) crowded = false;
  }
  if (crowded && v->open && !v->cue) {
    v->pref_drop = true;
    v->open = false;
    v->restart = false;
    v->seek = false;
    v->kill = false;
    v->defer_restart = false;
    v->steal_after = false;
    v->release_left = 0;
    return;
  }
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    if (i == voice) continue;
    SampleSlotVoice *o = &sample_slot_voice[i];
    if (o->sample != slot) continue;
    if (o->cue) {
      o->cue = false;
      o->restart = false;
    }
    if (!o->open) continue;
    o->restart = false;
    o->kill = true;
    if (!o->release_left) {
      o->release_len = SAMPLE_SLOT_XFADE;
      o->release_left = SAMPLE_SLOT_XFADE;
    }
  }
  v->sample = slot;
  bool primed = v->cue && v->open && v->file_sample == slot;
  v->cue = false;
  v->filter_reset = true;
  v->attack_pos = 0;
  v->decay_pos = 0;
  v->attack_len = sample_slot_attack[slot];
  v->decay_len = sample_slot_decay[slot];
  v->release_len = sample_slot_release[slot] ? sample_slot_release[slot]
                                            : SAMPLE_SLOT_ENV_MIN;
  v->started = ++sample_slot_stamp;
  bool overlapped = false;
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    SampleSlotVoice *o = &sample_slot_voice[i];
    if (o == v) continue;
    if (o->open || o->restart || o->release_left) overlapped = true;
  }
  if (primed) {
    if (v->phase != phase) {
      v->phase = phase;
      v->seek = true;
    }
    v->restart = false;
    v->release_left = 0;
    v->defer_restart = false;
    return;
  }
  if (v->open && v->file_sample == slot) {
    v->filter_reset = false;
    v->pending_phase = phase;
    v->defer_restart = true;
    v->release_len = SAMPLE_SLOT_XFADE;
    v->release_left = SAMPLE_SLOT_XFADE;
    if (v->attack_len < SAMPLE_SLOT_XFADE) v->attack_len = SAMPLE_SLOT_XFADE;
    return;
  }
  if (overlapped && v->attack_len < SAMPLE_SLOT_XFADE)
    v->attack_len = SAMPLE_SLOT_XFADE;
  v->phase = phase;
  v->card_after = 0;
  v->kill = false;
  v->restart = true;
  v->primed = false;
  v->have_ahead = false;
  v->frac_q16 = 0;
  v->hold_run = 0;
  v->resume_fade = 0;
  v->play_end = 0;
  v->release_left = 0;
  v->defer_restart = false;
}

static inline void sample_slot_arm_velocity(SampleSlotVoice *v, uint8_t velocity) {
  if (!velocity) velocity = 127;
  if (v->defer_restart) {
    v->pending_velocity = velocity;
    return;
  }
  v->play_velocity = velocity;
}

static inline bool sample_slot_playable(uint8_t slot, uint32_t *size,
                                        uint32_t *frame) {
  *size = 0;
  *frame = 2;
  int ord = sample_slot_ord(slot);
  if (ord < 0 || !banks[project_bank]) return false;
  SampleInfo *info = banks[project_bank]->sample[ord].snd[FILEZERO];
  if (!info || !info->size) return false;
  uint32_t fr = 2u;
  uint32_t sz = info->size - (info->size % fr);
  if (!sz) return false;
  *size = sz;
  *frame = fr;
  return true;
}

static inline void sample_slot_slice_bounds(uint8_t slot, uint8_t slice,
                                           uint32_t *start, uint32_t *end) {
  *start = 0;
  *end = 0;
  uint32_t size, frame;
  if (!sample_slot_playable(slot, &size, &frame)) return;
  if (slice > 15) slice = 15;
  uint32_t lo = sample_slot_region_custom[slot] ? sample_slot_region_start[slot] : 0;
  uint32_t hi = sample_slot_region_end_on[slot] ? sample_slot_region_end[slot] : size;
  if (lo > size) lo = size;
  if (hi > size) hi = size;
  if (hi < lo) hi = lo;
  uint32_t span = hi - lo;
  uint32_t s = lo + ((uint32_t)slice * span) / 16u;
  s -= s % frame;
  uint32_t e = slice == 15 ? hi : lo + ((uint32_t)(slice + 1u) * span) / 16u;
  e -= e % frame;
  if (e < s) e = s;
  *start = s;
  *end = e;
}

static inline void sample_slot_keep_order(uint32_t *start, uint32_t *end,
                                         uint32_t size, uint32_t frame) {
  *start -= *start % frame;
  *end -= *end % frame;
  if (*start > size) *start = size - (size % frame);
  if (*end > size) *end = size;
  if (*end >= *start + frame) return;
  if (*start + frame <= size) {
    *end = *start + frame;
    return;
  }
  if (size >= frame) {
    *end = size;
    *start = size - frame;
    return;
  }
  *start = 0;
  *end = size;
}

static inline void sample_slot_region(uint8_t slot, uint8_t slice,
                                     bool slice_mode, uint32_t *start,
                                     uint32_t *end) {
  *start = 0;
  *end = 0;
  uint32_t size, frame;
  if (!sample_slot_playable(slot, &size, &frame)) return;
  if (slice_mode) {
    if (slice > 15) slice = 15;
    sample_slot_slice_bounds(slot, slice, start, end);
    if (sample_slot_slice_custom[slot][slice])
      *start = sample_slot_slice_start[slot][slice];
    if (sample_slot_slice_end_on[slot][slice])
      *end = sample_slot_slice_end[slot][slice];
  } else {
    *end = size;
    if (sample_slot_region_custom[slot]) *start = sample_slot_region_start[slot];
    if (sample_slot_region_end_on[slot]) *end = sample_slot_region_end[slot];
  }
  sample_slot_keep_order(start, end, size, frame);
}

static inline uint32_t sample_slot_zero_snap(uint8_t slot, uint32_t pos,
                                            uint32_t size, uint32_t frame) {
  bool sample_slot_direct_ready(void);
  bool sample_slot_direct_read(uint8_t slot, uint32_t offset, uint8_t *dst,
                               uint32_t len, uint32_t *got);
  if (!sample_slot_direct_ready() || frame < 2 || size < frame * 2u || pos > size)
    return pos;
  const uint32_t radius = 256u;
  uint32_t from = pos > radius ? pos - radius : 0;
  from -= from % frame;
  uint32_t to = pos + radius;
  if (to > size) to = size - (size % frame);
  else to -= to % frame;
  if (to < from + frame * 2u) return pos;
  uint32_t bytes = to - from;
  if (bytes > 512u) bytes = 512u;
  bytes -= bytes % frame;
  uint8_t buf[512];
  uint32_t got = 0;
  if (!sample_slot_direct_read(slot, 88576u + from, buf, bytes, &got) ||
      got < frame * 2u)
    return pos;
  uint32_t frames = got / frame;
  int32_t best = -1;
  uint32_t best_dist = 0xffffffffu;
  for (uint32_t i = 1; i < frames; i++) {
    int16_t a = (int16_t)(buf[(i - 1u) * frame] | (buf[(i - 1u) * frame + 1u] << 8));
    int16_t b = (int16_t)(buf[i * frame] | (buf[i * frame + 1u] << 8));
    if (!((a <= 0 && b > 0) || (a >= 0 && b < 0) || a == 0)) continue;
    uint32_t at = a == 0 ? from + (i - 1u) * frame : from + i * frame;
    uint32_t dist = at > pos ? at - pos : pos - at;
    if (dist < best_dist) {
      best_dist = dist;
      best = (int32_t)at;
    }
  }
  if (best < 0) return pos;
  return (uint32_t)best;
}

static inline void sample_slot_set_bound(uint8_t slot, bool is_end,
                                        uint16_t adc) {
  uint32_t size, frame, start, end;
  if (!sample_slot_playable(slot, &size, &frame)) return;
  bool slice_mode = sample_slot_slice_mode[slot];
  uint8_t slice = sample_slot_slice_sel[slot];
  if (slice > 15) slice = 15;
  sample_slot_region(slot, slice, slice_mode, &start, &end);
  const uint16_t pad = 200;
  uint16_t knob = adc;
  if (knob <= pad)
    knob = 0;
  else if (knob >= 4095 - pad)
    knob = 4095;
  else
    knob = (uint16_t)((uint32_t)(knob - pad) * 4095u / (4095u - 2u * pad));
  uint32_t step = (uint32_t)knob * 255u / 4095u;
  uint32_t pos;
  if (!is_end) {
    pos = step * size / 255u;
  } else {
    uint32_t span_hi = size;
    if (span_hi < start) span_hi = start;
    pos = start + step * (span_hi - start) / 255u;
  }
  pos -= pos % frame;
  pos = sample_slot_zero_snap(slot, pos, size, frame);
  if (!is_end) {
    start = pos;
    if (end >= frame && start + frame > end) start = end - frame;
  } else {
    end = pos;
    if (end < start + frame) end = start + frame <= size ? start + frame : size;
  }
  if (slice_mode) {
    if (is_end) {
      sample_slot_slice_end_on[slot][slice] = true;
      sample_slot_slice_end[slot][slice] = end;
    } else {
      sample_slot_slice_custom[slot][slice] = true;
      sample_slot_slice_start[slot][slice] = start;
    }
  } else if (is_end) {
    sample_slot_region_end_on[slot] = true;
    sample_slot_region_end[slot] = end;
  } else {
    sample_slot_region_custom[slot] = true;
    sample_slot_region_start[slot] = start;
  }
  if (is_end) {
    for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
      if (sample_slot_voice[voice].sample == slot)
        sample_slot_voice[voice].play_end = end;
    }
  }
  project_mark();
}

uint32_t sample_slot_fine_start;
uint32_t sample_slot_fine_end;

static inline void sample_slot_fine_capture(uint8_t slot) {
  uint8_t slice = sample_slot_slice_sel[slot];
  if (slice > 15) slice = 15;
  sample_slot_region(slot, slice, sample_slot_slice_mode[slot],
                     &sample_slot_fine_start, &sample_slot_fine_end);
}

static inline void sample_slot_fine_bound(uint8_t slot, bool is_end,
                                         uint16_t adc) {
  uint32_t size, frame, start, end;
  if (!sample_slot_playable(slot, &size, &frame)) return;
  int32_t delta = (int32_t)adc - 2048;
  int32_t shift = (int32_t)((int64_t)delta * (int64_t)size / 10 / 2048);
  start = sample_slot_fine_start;
  end = sample_slot_fine_end;
  if (!is_end) {
    int32_t pos = (int32_t)start + shift;
    if (pos < 0) pos = 0;
    start = (uint32_t)pos;
  } else {
    int32_t pos = (int32_t)end + shift;
    if (pos < 0) pos = 0;
    end = (uint32_t)pos;
  }
  bool slice_mode = sample_slot_slice_mode[slot];
  uint8_t slice = sample_slot_slice_sel[slot];
  if (slice > 15) slice = 15;
  if (!is_end) {
    if (end >= frame && start + frame > end) start = end - frame;
    start -= start % frame;
    if (start > size) start = size - (size % frame);
    start = sample_slot_zero_snap(slot, start, size, frame);
    if (end >= frame && start + frame > end) start = end - frame;
  } else {
    if (end > size) end = size;
    if (end < start + frame) end = start + frame <= size ? start + frame : size;
    end -= end % frame;
    end = sample_slot_zero_snap(slot, end, size, frame);
    if (end < start + frame) end = start + frame <= size ? start + frame : size;
  }
  if (slice_mode) {
    if (is_end) {
      sample_slot_slice_end_on[slot][slice] = true;
      sample_slot_slice_end[slot][slice] = end;
    } else {
      sample_slot_slice_custom[slot][slice] = true;
      sample_slot_slice_start[slot][slice] = start;
    }
  } else if (is_end) {
    sample_slot_region_end_on[slot] = true;
    sample_slot_region_end[slot] = end;
  } else {
    sample_slot_region_custom[slot] = true;
    sample_slot_region_start[slot] = start;
  }
  if (is_end) {
    for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
      if (sample_slot_voice[voice].sample == slot)
        sample_slot_voice[voice].play_end = end;
    }
  }
  project_mark();
}

static inline void sample_slot_preview(uint8_t slot, uint8_t pad, bool fade_in) {
  int voice = sample_slot_claim(slot);
  if (voice < 0) return;
  int pitch;
  uint32_t phase = 0, end = 0;
  if (sample_slot_slice_mode[slot]) {
    if (pad > 15) pad = 15;
    sample_slot_slice_sel[slot] = pad;
    sample_slot_region(slot, pad, true, &phase, &end);
    pitch = sample_slot_active_pitch[slot];
  } else if (fade_in) {
    pitch = sample_slot_active_pitch[slot];
    sample_slot_region(slot, 0, false, &phase, &end);
  } else {
    int8_t semitone = pad < 16 ? sample_slot_pad_semitone[pad] : 0;
    pitch = (int)sample_slot_pitch[slot] + semitone;
    if (pitch > 12) pitch = 12;
    if (pitch < -12) pitch = -12;
    sample_slot_active_pitch[slot] = (int8_t)pitch;
    sample_slot_region(slot, 0, false, &phase, &end);
  }
  SampleSlotVoice *v = &sample_slot_voice[voice];
  v->play_pitch = (int8_t)pitch;
  v->play_filter = sample_slot_filter[slot];
  v->play_filter_q = sample_slot_filter_q[slot];
  v->play_filter_glide = false;
  v->filter_dirty = true;
  sample_slot_trigger_voice((uint8_t)voice, slot, phase);
  sample_slot_arm_velocity(v, sample_slot_level[slot]);
  if (sample_slot_voice[voice].defer_restart)
    sample_slot_voice[voice].pending_end = end;
  else
    sample_slot_voice[voice].play_end = end;
  if (fade_in) {
    sample_slot_voice[voice].attack_len = 352;
    sample_slot_voice[voice].attack_pos = 0;
  }
}

static inline void sample_slot_preview_end(uint8_t slot) {
  int voice = sample_slot_claim(slot);
  if (voice < 0) return;
  uint32_t size, frame, phase = 0, end = 0;
  if (!sample_slot_playable(slot, &size, &frame)) return;
  uint8_t slice = sample_slot_slice_sel[slot];
  if (slice > 15) slice = 15;
  sample_slot_region(slot, slice, sample_slot_slice_mode[slot], &phase, &end);
  uint32_t back = 44100u * frame;
  uint32_t from = end > back ? end - back : 0;
  if (from < phase) from = phase;
  SampleSlotVoice *v = &sample_slot_voice[voice];
  v->play_pitch = sample_slot_active_pitch[slot];
  v->play_filter = sample_slot_filter[slot];
  v->play_filter_q = sample_slot_filter_q[slot];
  v->play_filter_glide = false;
  v->filter_dirty = true;
  sample_slot_trigger_voice((uint8_t)voice, slot, from);
  sample_slot_arm_velocity(v, sample_slot_level[slot]);
  if (sample_slot_voice[voice].defer_restart)
    sample_slot_voice[voice].pending_end = end;
  else
    sample_slot_voice[voice].play_end = end;
}

static inline void sample_slot_fire_note(const SampleSlotNote *note) {
  uint32_t phase = 0, end = 0;
  if (sample_slot_slice_mode[note->sample])
    sample_slot_region(note->sample, note->slice, true, &phase, &end);
  else
    sample_slot_region(note->sample, 0, false, &phase, &end);
  int voice = sample_slot_claim_cued(note->sample, phase);
  if (voice < 0) voice = sample_slot_claim(note->sample);
  if (voice < 0) return;
  SampleSlotVoice *v = &sample_slot_voice[voice];
  if (note->sample < 16 && note->sample != sample_slot_edit)
    sample_slot_led_flash_until[note->sample] = time_us_32() + 50000u;
  v->play_pitch = note->pitch;
  v->play_filter = note->filter;
  v->play_filter_q = note->filter_q;
  v->play_filter_glide = false;
  v->filter_dirty = true;
  sample_slot_trigger_voice((uint8_t)voice, note->sample, phase);
  sample_slot_arm_velocity(v, note->velocity);
  if (sample_slot_voice[voice].defer_restart)
    sample_slot_voice[voice].pending_end = end;
  else
    sample_slot_voice[voice].play_end = end;
}

static inline void sample_slot_fire_step(uint8_t step, bool nudged) {
  if (sample_slot_pattern_sel > 15 ||
      !(sample_slot_pattern_mask & (uint16_t)(1u << sample_slot_pattern_sel)))
    return;
  uint8_t unread = 0;
  for (uint16_t i = 0; i < sample_slot_note_count; i++) {
    SampleSlotNote *n = &sample_slot_notes[i];
    if (n->pattern != sample_slot_pattern_sel || n->step != step) continue;
    if (nudged) {
      if (n->nudge > 0 && n->nudge == sample_slot_ticks) sample_slot_fire_note(n);
    } else if (n->nudge == 0) {
      sample_slot_fire_note(n);
      bool needs_read = false;
      for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
        SampleSlotVoice *v = &sample_slot_voice[vi];
        if (v->sample != n->sample || (!v->restart && !v->defer_restart)) continue;
        v->card_after = time_us_32() + (uint32_t)(unread / 2u) * 10000u;
        needs_read = true;
      }
      if (needs_read) unread++;
    }
  }
}

static inline void sample_slot_silence(void) {
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    SampleSlotVoice *v = &sample_slot_voice[i];
    v->defer_restart = false;
    v->restart = false;
    if (v->cue) {
      v->cue = false;
      v->open = false;
      v->kill = true;
      continue;
    }
    if (v->open) {
      v->kill = true;
      if (!v->release_left) {
        uint16_t rel = sample_slot_release[v->sample];
        v->release_len = rel ? rel : SAMPLE_SLOT_ENV_MIN;
        v->release_left = v->release_len;
      }
    } else {
      v->kill = true;
    }
  }
}

static inline void sample_slot_advance(void) {
  if ((!sample_slot_playing && !sample_slot_resample_busy) ||
      mode_buttons16 != MODE_SAMPLE_SLOT)
    return;
  if (++sample_slot_ticks < SAMPLE_SLOT_TICKS_PER_STEP) {
    sample_slot_fire_step(sample_slot_step, true);
    return;
  }
  sample_slot_ticks = 0;
  uint8_t chain_len = sample_slot_chain_len;
  bool pattern_empty =
      sample_slot_pattern_sel > 15 ||
      !(sample_slot_pattern_mask & (uint16_t)(1u << sample_slot_pattern_sel));
  if (sample_slot_step == 15 && sample_slot_chain_pending) {
    sample_slot_chain_pending = false;
    uint8_t next_pat =
        chain_len > 0 ? sample_slot_chain[0] : sample_slot_pattern_next;
    sample_slot_pattern_sel = next_pat;
    sample_slot_pattern_next = next_pat;
    sample_slot_chain_pos = 0;
    sample_slot_chain_entered = chain_len > 0;
  } else if (sample_slot_step == 15 && chain_len > 0 &&
      !(pattern_empty && !sample_slot_chain_entered)) {
    uint8_t next_pos = 0;
    if (sample_slot_chain_entered) {
      next_pos = (uint8_t)(sample_slot_chain_pos + 1u);
      if (next_pos >= chain_len) next_pos = 0;
    }
    uint8_t next_pat = sample_slot_chain[next_pos];
    sample_slot_pattern_sel = next_pat;
    sample_slot_chain_pos = next_pos;
    sample_slot_chain_entered = true;
    sample_slot_pattern_next = next_pat;
  } else if (sample_slot_step == 15 &&
             sample_slot_pattern_next != sample_slot_pattern_sel) {
    sample_slot_pattern_sel = sample_slot_pattern_next;
  }
  sample_slot_step = (sample_slot_step + 1) & 15;
  sample_slot_fire_step(sample_slot_step, false);
}

// ignore boundaries
#define PLAY_NORMAL 0
// starts at splice start and ends at splice stop
#define PLAY_SPLICE_STOP 1
// starts at splice start, and returns to start when reaching splice boundary
#define PLAY_SPLICE_LOOP 2
// starts at splice start and ends at sample boundary
#define PLAY_SAMPLE_STOP 3
// starts at splice start and returns to start when reaching sample boundary
#define PLAY_SAMPLE_LOOP 4


#ifdef INCLUDE_RGBLED
WS2812 *ws2812;
#endif

uint8_t key_jump_debounce = 0;
bool do_random_jump = false;
bool jump_precedence = false;
uint32_t beat_current_last = 0;


void update_repeating_timer_to_bpm(float bpm) {
  // calibrated
  // BPM	Beats	Time	  Measured BPM	Difference
  // 90	  160	  106.022	90.54724491	1.006080499
  // 120	96	  47.705	120.7420606	1.006183838
  // 160	160	  59.625	161.0062893	1.006289308
  // 180	160	  53	    181.1320755	1.006289308
  // average difference = 1.006210738
  // 312500 * 1.006210738 = 314441.043
  if (bpm < 30.f) bpm = 30.f;
  if (bpm > 300.f) bpm = 300.f;
  cancel_repeating_timer(&timer);
  add_repeating_timer_us(-(round(314441.0f / bpm)), repeating_timer_callback,
                         NULL, &timer);
  // original
  // add_repeating_timer_us(-(round(312500.0f / bpm)), repeating_timer_callback,
  //                        NULL, &timer);
}

void timer_reset() {
  cancel_repeating_timer(&timer);
  do_restart_playback = true;
  timer_step();
#ifdef INCLUDE_ZEPTOCORE
  update_repeating_timer_to_bpm(project_bpm);
#else
  update_repeating_timer_to_bpm(sf->bpm_tempo);
#endif
  button_mute = false;
}
#endif
