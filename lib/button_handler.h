// Copyright 2023-2025 Zack Scholl, GPLv3.0

#include "globals.h"
#include "project.h"
#include "usb_record.h"

// keys
#define KEY_A 0
#define KEY_B 1
#define KEY_C 2
#define KEY_D 3

uint8_t key_held_num = 0;
bool key_held_on = false;
int32_t key_timer = 0;
int32_t key_timer_on = 0;
uint8_t key_pressed[100];
uint8_t key_pressed_num = 0;
uint8_t key_total_pressed = 0;
uint8_t key_total_pressed_last = 0;
int16_t key_on_buttons[BUTTONMATRIX_BUTTONS_MAX];
int16_t key_on_buttons_last[BUTTONMATRIX_BUTTONS_MAX];
bool key_did_go_off[BUTTONMATRIX_BUTTONS_MAX];
uint16_t key_num_presses;
bool sample_slot_note_d_alone_candidate = false;
static bool sample_slot_d_record_hold;
bool sample_slot_a_play_candidate = false;
static uint8_t sample_slot_paste;
static bool sample_slot_paste_arm;

static void sample_slot_paste_exit(void) {
  sample_slot_paste = 0;
  sample_slot_paste_arm = false;
}

static void sample_slot_paste_pad(uint8_t pad) {
  if (pad > 15 || !sample_slot_paste) return;
  if (sample_slot_paste == 1) {
    if (pad == sample_slot_edit) {
      if (!sample_slot_paste_arm) {
        sample_slot_paste_arm = true;
        return;
      }
      sample_slot_silence();
      if (!sample_slot_transfer(sample_slot_edit, sample_slot_edit)) return;
    } else {
      sample_slot_silence();
      if (!sample_slot_transfer(sample_slot_edit, pad)) return;
      sample_slot_edit = pad;
      sample_slot_active = sample_slot_edit;
      sample_slot_step_mask_refresh();
    }
  } else if (pad == sample_slot_pattern_sel) {
    if (!sample_slot_paste_arm) {
      sample_slot_paste_arm = true;
      return;
    }
    sample_slot_pattern_copy(pad);
  } else {
    sample_slot_pattern_copy(pad);
  }
  sample_slot_paste_exit();
}

bool button_is_pressed(uint8_t key) { return key_on_buttons[key] > 0; }

void maybe_toggle_jump_page_lock_on_d_release(uint8_t key) {
  if (key != KEY_D) {
    return;
  }
  if (sample_slot_note_d_alone_candidate && mode_buttons16 == MODE_SAMPLE_SLOT &&
      !sample_slot_d_record_hold) {
    sample_slot_note_mode = !sample_slot_note_mode;
  }
  sample_slot_note_d_alone_candidate = false;
  sample_slot_d_record_hold = false;
}

static bool project_skip_a_release;
static int8_t project_pad_candidate = -1;
static bool project_pad_busy;

static void project_pad_release(uint8_t slot) {
  if (project_pad_busy) return;
  project_pad_busy = true;
  if (project_confirm) {
    if (slot == project_bank) {
      if (project_clear()) project_confirm = false;
    } else if (project_select(slot)) {
      project_mode = false;
      project_confirm = false;
      project_pad_candidate = -1;
    }
  } else if (slot == project_bank) {
    project_confirm = true;
  } else if (project_select(slot)) {
    project_mode = false;
    project_confirm = false;
    project_pad_candidate = -1;
  }
  project_pad_busy = false;
}

void maybe_toggle_sample_slot_play_on_a_release(uint8_t key) {
  if (key != KEY_A) return;
  if (project_skip_a_release) {
    project_skip_a_release = false;
    sample_slot_a_play_candidate = false;
    return;
  }
  if (project_mode) {
    project_mode = false;
    project_confirm = false;
    project_pad_candidate = -1;
    sample_slot_a_play_candidate = false;
    return;
  }
  if (!(sample_slot_a_play_candidate && mode_buttons16 == MODE_SAMPLE_SLOT)) {
    sample_slot_a_play_candidate = false;
    return;
  }
  sample_slot_a_play_candidate = false;
  if (!sample_slot_playing && sample_slot_chain_len == 0 &&
      sample_slot_chain_edit_len > 0) {
    uint8_t chain_n = sample_slot_chain_edit_len;
    if (chain_n > 64) chain_n = 64;
    for (uint8_t i = 0; i < chain_n; i++)
      sample_slot_chain[i] = sample_slot_chain_edit[i];
    sample_slot_chain_len = chain_n;
    sample_slot_chain_pos = 0;
    project_mark();
  }
  sample_slot_playing = !sample_slot_playing;
  if (sample_slot_playing) {
    if (sample_slot_chain_len > 0) {
      sample_slot_pattern_sel = sample_slot_chain[0];
      sample_slot_pattern_next = sample_slot_chain[0];
      sample_slot_chain_pos = 0;
      sample_slot_chain_entered = true;
      sample_slot_chain_pending = false;
      sample_slot_step_mask_refresh();
    }
    sample_slot_ticks = 0;
    sample_slot_step = 0;
    sample_slot_fire_step(0, false);
  } else {
    sample_slot_silence();
    if (sample_slot_chain_len > 0 && sample_slot_chain[0] < 16) {
      sample_slot_pattern_sel = sample_slot_chain[0];
      sample_slot_pattern_next = sample_slot_chain[0];
      sample_slot_chain_pos = 0;
      sample_slot_chain_entered = false;
      sample_slot_chain_pending = false;
      sample_slot_step_mask_refresh();
    }
  }
}



void do_button_lights(ButtonMatrix *bm) {
  uint32_t ct = to_ms_since_boot(get_absolute_time());
  for (uint8_t i = 0; i < 20; i++) {
    if (key_on_buttons[i] > 0) {
      LEDS_set(leds, i, LED_BRIGHT);
    } else {
      if (i > 3 && !bm->button_on[i] && ct > bm->off_time[i] &&
          ct - bm->off_time[i] < 200) {
        // printf("[%ld] btn %d off time: %ld\n", i, ct, bm->off_time[i]);
        LEDS_set(leds, i, 0);
      }
    }
  }
}



void sample_slot_reserve_deny(void) {
  if (!leds) return;
  for (uint8_t n = 0; n < 2; n++) {
    LEDS_clear(leds);
    for (uint8_t i = 4; i < 20; i++) LEDS_set(leds, i, LED_BRIGHT);
    LEDS_render(leds);
    sleep_ms(120);
    LEDS_clear(leds);
    LEDS_render(leds);
    sleep_ms(80);
  }
}

void sample_slot_copy_bar(uint8_t lit) {
  static const uint8_t order[16] = {16, 17, 18, 19, 12, 13, 14, 15,
                                     8,  9,  10, 11, 4,  5,  6,  7};
  if (!leds) return;
  if (lit > 16) lit = 16;
  LEDS_clear(leds);
  for (uint8_t i = 0; i < lit; i++) LEDS_set(leds, order[i], LED_BRIGHT);
  LEDS_render(leds);
}

void button_key_off_held(uint8_t key) {}

// triggers on ANY key off, used for 1-16 off's
void button_key_off_any(uint8_t key) {
  if (key_total_pressed < 3) {
    key3_activated = false;
  }
  if (key == KEY_C && mode_buttons16 == MODE_SAMPLE_SLOT &&
      sample_slot_chain_edit_len > 0) {
    sample_slot_chain_publish = true;
    sample_slot_chain_hold_save = true;
  }
  if (key > 3) {
    if (mode_buttons16 == MODE_SAMPLE_SLOT && project_mode && key < 20) {
      uint8_t slot = key - 4;
      if (project_pad_candidate == (int8_t)slot && key_total_pressed == 0)
        project_pad_release(slot);
      project_pad_candidate = -1;
    } else if (mode_buttons16 == MODE_SAMPLE_SLOT && key_total_pressed == 0) {
      if (!sample_slot_hold_edited && key < 20)
        sample_slot_preview(sample_slot_edit, key - 4, false);
      sample_slot_hold_edited = false;
    }
    if (key_total_pressed == 0) {
      if (mode_hands_on_unmute) {
        if (!button_mute) {
          trigger_button_mute = true;
        }
      }
      dub_step_break = -1;
    }
  }
}

uint32_t tap_tempo_last = 0;
uint8_t tap_tempo_hits = 0;

void button_key_on_single(uint8_t key) {
  if (project_mode && key == KEY_D) {
    project_mode = false;
    project_confirm = false;
    project_pad_candidate = -1;
    sample_slot_note_d_alone_candidate = false;
    return;
  }
  if (sample_slot_resample_pick && key == KEY_D) {
    sample_slot_resample_pick = false;
    return;
  }
  if (key == KEY_D && sample_slot_paste) {
    sample_slot_paste_exit();
    sample_slot_note_d_alone_candidate = false;
    return;
  }
  if (mode_buttons16 == MODE_SAMPLE_SLOT && key == KEY_C) {
    sample_slot_chain_restart = true;
    sample_slot_chain_edit_len = 0;
  }
  if (key < 4) {
  } else if (key >= 4) {
    if (mode_hands_on_unmute) {
      if (button_mute) {
        button_mute = false;
        trigger_button_mute = false;
      }
    }
    if (mode_buttons16 == MODE_SAMPLE_SLOT) {
      if (sample_slot_resample_pick) {
        uint8_t slot = key - 4;
        sample_slot_hold_edited = true;
        if (sample_slot_ord(slot) < 0) sample_slot_resample_run(slot);
        return;
      }
      if (project_mode) {
        if (!project_pad_busy) project_pad_candidate = key - 4;
        return;
      }
      if (sample_slot_paste) {
        sample_slot_paste_pad(key - 4);
        sample_slot_hold_edited = true;
        return;
      }
      if (sample_slot_note_mode) {
        sample_slot_note_toggle(sample_slot_edit, key - 4);
        sample_slot_hold_edited = true;
      } else {
        // A tap selects and plays on release. A hold can edit step timing.
        sample_slot_hold_edited = false;
      }
    }
  }
}

void button_key_on_double(uint8_t key1, uint8_t key2) {
  if (project_mode && key2 >= 4) return;
  if (key_on_buttons[KEY_A] && key_on_buttons[KEY_D]) {
    return;
  }
  if (key1 == KEY_A) {
    if (key2 == KEY_D) {
      // S+C. Key 0 held with both 2 and 3 is sample slot mode.
      if (key_on_buttons[KEY_C] > 0) {
        mode_buttons16 = MODE_SAMPLE_SLOT;
        sample_slot_note_mode = false;
      } else {
        project_mode = !project_mode;
        project_confirm = false;
        project_pad_candidate = -1;
        sample_slot_a_play_candidate = false;
        sample_slot_note_mode = false;
        if (project_mode) {
          mode_buttons16 = MODE_SAMPLE_SLOT;
          project_skip_a_release = true;
          project_select_prepare();
        }
      }
    }
  } else if (key1 == KEY_B) {
    if (mode_buttons16 == MODE_SAMPLE_SLOT && key2 == KEY_D) {
      sample_slot_paste = 1;
      sample_slot_paste_arm = false;
      sample_slot_note_d_alone_candidate = false;
      return;
    }
    if (mode_buttons16 == MODE_SAMPLE_SLOT && key2 > 3) {
      if (sample_slot_paste == 1) {
        sample_slot_paste_pad(key2 - 4);
        return;
      }
      sample_slot_edit = key2 - 4;
      sample_slot_active = sample_slot_edit;
      sample_slot_step_mask_refresh();
      return;
    }
    // A
  } else if (key1 == KEY_C) {
    // B
    if (key2 == KEY_B) {
      if (mode_buttons16 == MODE_SAMPLE_SLOT) return;
    } else if (key2 == KEY_D) {
      if (mode_buttons16 == MODE_SAMPLE_SLOT) {
        sample_slot_paste = 2;
        sample_slot_paste_arm = false;
        sample_slot_note_d_alone_candidate = false;
        return;
      }
    } else if (key2 > 3) {
      if (mode_buttons16 == MODE_SAMPLE_SLOT) {
        uint8_t pattern = key2 - 4;
        if (sample_slot_paste == 2) {
          sample_slot_paste_pad(pattern);
          return;
        }
        bool first = sample_slot_chain_restart;
        uint8_t len = first ? 0 : sample_slot_chain_edit_len;
        if (len < 64) {
          sample_slot_chain_edit[len] = pattern;
          sample_slot_chain_edit_len = (uint8_t)(len + 1);
        }
        if (first) sample_slot_chain_restart = false;
        if (sample_slot_playing && sample_slot_chain_entered &&
            sample_slot_chain_len > 0)
          sample_slot_chain_publish = true;
        return;
      }
    }
  } else if (key1 == KEY_D) {
    if (key2 == KEY_C && mode_buttons16 == MODE_SAMPLE_SLOT &&
        !sample_slot_playing && !project_mode) {
      sample_slot_resample_pick = !sample_slot_resample_pick;
      return;
    }
    if (sample_slot_resample_pick) {
      if (key2 >= 4) {
        uint8_t slot = key2 - 4;
        sample_slot_hold_edited = true;
        if (sample_slot_ord(slot) < 0) sample_slot_resample_run(slot);
      }
      return;
    }
    if (key2 >= 4 && mode_buttons16 == MODE_SAMPLE_SLOT) {
      if (sample_slot_playing) {
        uint8_t pad = key2 - 4;
        sample_slot_live_note(pad);
        sample_slot_preview(sample_slot_edit, pad, false);
        sample_slot_note_d_alone_candidate = false;
      } else {
        usb_record_arm(key2 - 4);
      }
      sample_slot_hold_edited = true;
    }
  }
}

bool button_handler(ButtonMatrix *bm) {
  bool buttons_changed = false;
  if (key_total_pressed == 0) {
    key_timer++;
  }
  if (key_timer == led_text_time && key_pressed_num > 0) {
    // // B + H + H...
    // // chain: select sequences to chain together
    // if (key_pressed[0] == KEY_C) {
    //   uint8_t *links = malloc(sizeof(uint8_t) * (key_pressed_num - 1));
    //   uint16_t count = 0;
    //   for (uint8_t i = 0; i < key_pressed_num; i++) {
    //     if (key_pressed[i] - 4 >= 0) {
    //       links[count] = key_pressed[i] - 4;
    //       count++;
    //     }
    //   }
    //   if (count > 0) {
    //     // toggle_chain_rec = false;
    //     // Chain_link(chain, links, count);
    //   }
    //   free(links);
    // }
    key_timer = 0;
    key_pressed_num = 0;
  }

#ifdef INCLUDE_BUTTONS
  // read the latest from the queue
  ButtonMatrix_read(bm);
#endif

  // check queue for buttons that turned off
  for (uint8_t i = 0; i < BUTTONMATRIX_BUTTONS_MAX; i++) {
    key_did_go_off[i] = false;
  }
  for (uint8_t i = 0; i < bm->off_num; i++) {
    key_total_pressed--;
    key_on_buttons_last[bm->off[i]] = key_on_buttons[bm->off[i]];
    key_on_buttons[bm->off[i]] = 0;
    key_did_go_off[bm->off[i]] = true;
    button_key_off_any(bm->off[i]);
    maybe_toggle_jump_page_lock_on_d_release(bm->off[i]);
    maybe_toggle_sample_slot_play_on_a_release(bm->off[i]);
    // printf("turned off %d\n", bm->off[i]);
    if (key_held_on && (bm->off[i] == key_held_num)) {
      button_key_off_held(bm->off[i]);

      key_held_on = false;
      // TODO:
      // use the last key pressed that is still held as the new hold
      if (key_total_pressed > 0) {
        uint16_t *indexes =
            sort_int16_t(key_on_buttons, BUTTONMATRIX_BUTTONS_MAX);
        key_held_on = true;
        key_held_num = indexes[BUTTONMATRIX_BUTTONS_MAX - 1];
        free(indexes);
      } else {
        key_num_presses = 0;
        for (uint8_t i = 0; i < BUTTONMATRIX_BUTTONS_MAX; i++) {
          key_on_buttons[i] = 0;
        }
      }
    }

    if (bm->off[i] < 4) {
    } else {
#ifdef INCLUDE_MIDI
      // send out midi notes
      MidiOut_off(midiout[mode_buttons16], bm->off[i] - 4);
#endif
    }
  }

  // check queue for buttons that turned on
  for (uint8_t i = 0; i < bm->on_num; i++) {
    key_total_pressed++;
    if (key_total_pressed == 1) {
      key_timer_on = 0;
    }
    if (bm->on[i] == KEY_D) {
      if (sample_slot_resample_pick && mode_buttons16 == MODE_SAMPLE_SLOT &&
          key_total_pressed == 1) {
        sample_slot_resample_pick = false;
        sample_slot_note_d_alone_candidate = false;
      } else {
        sample_slot_note_d_alone_candidate =
            mode_buttons16 == MODE_SAMPLE_SLOT && key_total_pressed == 1;
      }
      if (mode_buttons16 == MODE_SAMPLE_SLOT)
        sample_slot_fine_capture(sample_slot_edit);
    } else if (key_on_buttons[KEY_D] > 0) {
      sample_slot_note_d_alone_candidate = false;
    }
    if (bm->on[i] == KEY_A) {
      sample_slot_a_play_candidate =
          mode_buttons16 == MODE_SAMPLE_SLOT && key_total_pressed == 1;
    } else if (key_on_buttons[KEY_A] > 0) {
      sample_slot_a_play_candidate = false;
    }
    if (!key_held_on) {
      key_held_on = true;
      key_held_num = bm->on[i];
      button_key_on_single(bm->on[i]);
    } else {
      button_key_on_double(key_held_num, bm->on[i]);
    }

    // keep track of combos
    key_pressed[key_pressed_num] = bm->on[i];
    if (key_pressed_num < 100) {
      key_pressed_num++;
    }
    key_timer = 0;

    // keep track of all
    key_num_presses++;
    key_on_buttons_last[bm->on[i]] = key_on_buttons[bm->on[i]];
    key_on_buttons[bm->on[i]] = key_num_presses;
    if (bm->on[i] < 4) {
    } else {
#ifdef INCLUDE_MIDI
      // send out midi notes
      MidiOut_on(midiout[mode_buttons16], bm->on[i] - 4, 127);
#endif
    }
  }

  if (key_total_pressed > 0) {
    key_timer_on++;
    if (key_total_pressed == 1) {
      single_key_on = key_pressed[0];
    } else {
      single_key_on = -1;
    }
  } else {
    single_key_on = -1;
  }

  if (key_total_pressed != key_total_pressed_last) {
    buttons_changed = true;
  }
  key_total_pressed_last = key_total_pressed;

  // rendering!
  if (ledtext && ledtext->debounce > 0) return buttons_changed;

  // leds
  LEDS_clear(leds);

  // check debouncers
  if (DebounceUint8_active(debouncer_uint8[DEBOUNCE_UINT8_LED_BAR])) {
    // show an LED bar
    const uint8_t led_bar_ordering[32] = {
        17, 18, 17, 18, 16, 16, 19, 19, 13, 13, 14, 15, 15, 14, 12, 12,
        9,  10, 10, 9,  8,  8,  11, 5,  5,  11, 6,  6,  7,  4,  4,  7};
    const bool led_bar_brightness[32] = {
        0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1,
    };
    for (uint8_t i = 0;
         i < linlin_uint8_t(
                 DebounceUint8_get(debouncer_uint8[DEBOUNCE_UINT8_LED_BAR]), 10,
                 240, 0, 32);
         i++) {
      if (led_bar_brightness[i]) {
        LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
      } else {
        LEDS_set(leds, led_bar_ordering[i], LED_DIM);
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceUint8_active(
                 debouncer_uint8[DEBOUNCE_UINT8_LED_SPIRAL1])) {
    // show an LED bar
    const uint8_t led_bar_ordering[32] = {
        10, 14, 13, 9, 5, 6, 7, 11, 15, 19, 18, 17, 16, 12, 8, 4,
        10, 14, 13, 9, 5, 6, 7, 11, 15, 19, 18, 17, 16, 12, 8, 4};
    const bool led_bar_brightness[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    for (uint8_t i = 0;
         i < linlin_uint8_t(
                 DebounceUint8_get(debouncer_uint8[DEBOUNCE_UINT8_LED_SPIRAL1]),
                 10, 240, 0, 32);
         i++) {
      if (led_bar_brightness[i]) {
        LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
      } else {
        LEDS_set(leds, led_bar_ordering[i], LED_DIM);
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceUint8_active(
                 debouncer_uint8[DEBOUNCE_UINT8_LED_RANDOM1])) {
    // show an LED bar
    const uint8_t led_bar_ordering[32] = {

        18, 11, 8, 4, 16, 7, 13, 19, 10, 5, 14, 15, 17, 12, 6,  9,
        13, 14, 6, 9, 16, 8, 18, 12, 11, 7, 10, 17, 15, 5,  19, 4};
    const bool led_bar_brightness[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    for (uint8_t i = 0;
         i < linlin_uint8_t(
                 DebounceUint8_get(debouncer_uint8[DEBOUNCE_UINT8_LED_RANDOM1]),
                 10, 240, 0, 32);
         i++) {
      if (led_bar_brightness[i]) {
        LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
      } else {
        LEDS_set(leds, led_bar_ordering[i], LED_DIM);
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceUint8_active(
                 debouncer_uint8[DEBOUNCE_UINT8_LED_RANDOM2])) {
    // show an LED bar
    const uint8_t led_bar_ordering[32] = {
        7,  16, 18, 14, 11, 5,  9, 12, 13, 15, 6,  4,  10, 8, 19, 17,
        18, 8,  7,  10, 9,  14, 6, 19, 11, 12, 16, 15, 17, 5, 4,  13};
    const bool led_bar_brightness[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    for (uint8_t i = 0;
         i < linlin_uint8_t(
                 DebounceUint8_get(debouncer_uint8[DEBOUNCE_UINT8_LED_RANDOM2]),
                 10, 240, 0, 32);
         i++) {
      if (led_bar_brightness[i]) {
        LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
      } else {
        LEDS_set(leds, led_bar_ordering[i], LED_DIM);
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceUint8_active(
                 debouncer_uint8[DEBOUNCE_UINT8_LED_TRIANGLE])) {
    uint8_t val =
        DebounceUint8_get(debouncer_uint8[DEBOUNCE_UINT8_LED_TRIANGLE]);
    if (val < 128 - 5) {
      uint8_t led_bar_ordering[12] = {
          8, 13, 18, 17, 12, 16, 8, 13, 18, 17, 12, 16,
      };
      bool led_bar_brightness[12] = {
          0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
      };
      for (uint8_t i = 0; i < linlin_uint8_t((128 - 5) - val, 0, 115, 0, 11);
           i++) {
        if (led_bar_brightness[i]) {
          LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
        } else {
          LEDS_set(leds, led_bar_ordering[i], LED_DIM);
        }
      }
      LEDS_set(leds, 4, LED_DIM);
      LEDS_set(leds, 9, LED_DIM);
      LEDS_set(leds, 14, LED_DIM);
      LEDS_set(leds, 19, LED_DIM);
    } else if (val > 128 + 5) {
      uint8_t led_bar_ordering[12] = {
          5, 10, 15, 6, 11, 7, 5, 10, 15, 6, 11, 7,
      };
      bool led_bar_brightness[12] = {
          0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
      };
      for (uint8_t i = 0; i < linlin_uint8_t(val, 128 + 5, 240, 0, 11); i++) {
        if (led_bar_brightness[i]) {
          LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
        } else {
          LEDS_set(leds, led_bar_ordering[i], LED_DIM);
        }
      }
      LEDS_set(leds, 4, LED_DIM);
      LEDS_set(leds, 9, LED_DIM);
      LEDS_set(leds, 14, LED_DIM);
      LEDS_set(leds, 19, LED_DIM);
    } else {
      LEDS_set(leds, 4, LED_BRIGHT);
      LEDS_set(leds, 9, LED_BRIGHT);
      LEDS_set(leds, 14, LED_BRIGHT);
      LEDS_set(leds, 19, LED_BRIGHT);
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceUint8_active(
                 debouncer_uint8[DEBOUNCE_UINT8_LED_DIAGONAL])) {
    // show an LED bar
    const uint8_t led_bar_ordering[32] = {
        16, 12, 17, 18, 8, 13, 4, 19, 9, 14, 5, 15, 10, 6, 11, 7,
        16, 12, 17, 18, 8, 13, 4, 19, 9, 14, 5, 15, 10, 6, 11, 7,
    };
    const bool led_bar_brightness[32] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    for (uint8_t i = 0;
         i < linlin_uint8_t(DebounceUint8_get(
                                debouncer_uint8[DEBOUNCE_UINT8_LED_DIAGONAL]),
                            10, 240, 0, 32);
         i++) {
      if (led_bar_brightness[i]) {
        LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
      } else {
        LEDS_set(leds, led_bar_ordering[i], LED_DIM);
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceDigits_active(debouncer_digits)) {
    char digit = DebounceDigits_get(debouncer_digits);

    for (int i = 0; i < sizeof(led_text_5x4) / sizeof(led_text_5x4[0]); i++) {
      if (led_text_5x4[i].character == digit) {
        int led = 0;
        for (int j = 0; j < 5; j++) {
          // print out 0 or 1
          for (int k = 0; k < 4; k++) {
            bool is_one = (led_text_5x4[i].dots[j] >> (3 - k)) & 1;
            if (is_one) {
              LEDS_set(leds, led, LED_BRIGHT);
            } else {
              LEDS_set(leds, led, 0);
            }
            led++;
          }
        }
        break;
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else if (DebounceUint8_active(debouncer_uint8[DEBOUNCE_UINT8_LED_WALL])) {
    // show an LED bar
    const uint8_t led_bar_ordering[32] = {
        16, 12, 8,  4, 4, 8,  12, 16, 17, 13, 9,  5,  5,  9,  13, 17,
        18, 14, 10, 6, 6, 10, 14, 18, 7,  11, 15, 19, 19, 15, 11, 7};
    const bool led_bar_brightness[32] = {
        0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
        0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    };
    for (uint8_t i = 0;
         i < linlin_uint8_t(
                 DebounceUint8_get(debouncer_uint8[DEBOUNCE_UINT8_LED_WALL]),
                 10, 240, 0, 32);
         i++) {
      if (led_bar_brightness[i]) {
        LEDS_set(leds, led_bar_ordering[i], LED_BRIGHT);
      } else {
        LEDS_set(leds, led_bar_ordering[i], LED_DIM);
      }
    }
    do_button_lights(bm);
    LEDS_render(leds);
    return buttons_changed;
  } else {
    if (key_on_buttons[KEY_C] || key_did_go_off[KEY_C]) {
      if (key_total_pressed == 1) {
        // for (uint8_t i = 0; i < 16; i++) {
        //   if (Chain_has_data(chain, i)) {
        //     LEDS_set(leds, i + 4, 1);
        //   }
        // }
        // LEDS_set(leds, Chain_get_current(chain) + 4, 3);
      }
    }
  }

  do_button_lights(bm);

  if (mode_buttons16 == MODE_SAMPLE_SLOT && project_mode) {
    for (uint8_t i = 0; i < 16; i++) {
      bool cross = i == 0 || i == 3 || i == 5 || i == 6 || i == 9 || i == 10 ||
                   i == 12 || i == 15;
      if (project_confirm && i == project_bank)
        LEDS_set(leds, i + 4, LED_BRIGHT);
      else if (project_confirm && cross)
        LEDS_set(leds, i + 4, LED_BLINK);
      else if (project_confirm)
        LEDS_set(leds, i + 4, LED_NONE);
      else
        LEDS_set(leds, i + 4,
                 i == project_bank ? LED_BLINK
                 : project_bank_used(i) ? LED_DIM
                                        : LED_NONE);
    }
  } else if (mode_buttons16 == MODE_SAMPLE_SLOT) {
    static uint8_t chain_step_lights_skip;
    bool chain_keys = key_on_buttons[KEY_C] || key_did_go_off[KEY_C];
    bool skip_steps = chain_keys || chain_step_lights_skip;
    chain_step_lights_skip = key_did_go_off[KEY_C] ? 1 : 0;
    if (!skip_steps) {
      uint16_t match = 0;
      if (!sample_slot_playing) {
        bool slice = sample_slot_slice_mode[sample_slot_edit];
        uint8_t slice_sel = sample_slot_slice_sel[sample_slot_edit];
        int8_t pitch = sample_slot_active_pitch[sample_slot_edit];
        for (uint16_t n = 0; n < sample_slot_note_count; n++) {
          SampleSlotNote *note = &sample_slot_notes[n];
          if (note->pattern != sample_slot_pattern_sel ||
              note->sample != sample_slot_edit || note->step > 15)
            continue;
          if (slice ? note->slice == slice_sel : note->pitch == pitch)
            match |= (uint16_t)(1u << note->step);
        }
      }
      for (uint8_t i = 0; i < 16; i++) {
        if (sample_slot_step_mask & (uint16_t)(1u << i))
          LEDS_set(leds, i + 4, LED_DIM);
      }
      if (sample_slot_playing)
        LEDS_set(leds, sample_slot_step + 4, LED_BRIGHT);
      else {
        for (uint8_t i = 0; i < 16; i++) {
          if (match & (uint16_t)(1u << i))
            LEDS_set(leds, i + 4, LED_BLINK);
        }
      }
    }
    if (!chain_keys && sample_slot_paste != 2 &&
        (key_on_buttons[KEY_B] || sample_slot_paste == 1)) {
      uint32_t now = time_us_32();
      for (uint8_t i = 0; i < 16; i++) {
        if (i == sample_slot_edit) {
          LEDS_set(leds, i + 4, LED_BLINK);
        } else if (sample_slot_ord(i) < 0) {
          LEDS_set(leds, i + 4, LED_NONE);
        } else if (sample_slot_playing &&
                   (int32_t)(sample_slot_led_flash_until[i] - now) > 0) {
          LEDS_set(leds, i + 4, LED_BRIGHT);
        } else {
          LEDS_set(leds, i + 4, LED_DIM);
        }
      }
    } else if (key_on_buttons[KEY_C] || sample_slot_paste == 2) {
      for (uint8_t i = 0; i < 16; i++) LEDS_set(leds, i + 4, LED_NONE);
      for (uint8_t i = 0; i < 16; i++) {
        if (sample_slot_pattern_stored(i)) LEDS_set(leds, i + 4, LED_DIM);
      }
      if (sample_slot_chain_edit_len == 0) {
        LEDS_set(leds, sample_slot_pattern_sel + 4, LED_BLINK);
        if (sample_slot_pattern_next != sample_slot_pattern_sel)
          LEDS_set(leds, sample_slot_pattern_next + 4, LED_BLINK);
      } else {
        for (uint8_t i = 0; i < sample_slot_chain_edit_len && i < 64; i++) {
          uint8_t pad = sample_slot_chain_edit[i];
          if (pad < 16) LEDS_set(leds, pad + 4, LED_DIM);
        }
        uint8_t last = sample_slot_chain_edit[sample_slot_chain_edit_len - 1];
        if (last < 16) LEDS_set(leds, last + 4, LED_BLINK);
      }
    }
    static uint32_t sample_slot_d_hold_us;
    if (key_on_buttons[KEY_D] && !sample_slot_playing &&
        !sample_slot_resample_pick) {
      uint32_t now = time_us_32();
      if (sample_slot_d_hold_us == 0) sample_slot_d_hold_us = now;
      if ((uint32_t)(now - sample_slot_d_hold_us) > 400000) {
        sample_slot_d_record_hold = true;
        for (uint8_t i = 0; i < 16; i++) {
          LEDS_set(leds, i + 4, sample_slot_ord(i) < 0 ? LED_NONE : LED_DIM);
        }
      }
    } else {
      sample_slot_d_hold_us = 0;
    }
    {
      int8_t armed = usb_record_pad();
      if (armed >= 0 && usb_record_started()) {
        for (uint8_t i = 0; i < 16; i++) LEDS_set(leds, i + 4, LED_NONE);
        LEDS_set(leds, armed + 4, LED_BRIGHT);
      } else if (armed >= 0) {
        LEDS_set(leds, armed + 4, LED_BLINK);
      }
    }
  }

  if (mode_buttons16 == MODE_SAMPLE_SLOT && sample_slot_resample_pick) {
    uint32_t now = time_us_32();
    for (uint8_t i = 0; i < 16; i++) {
      if (sample_slot_ord(i) < 0) {
        LEDS_set(leds, i + 4, LED_NONE);
      } else if (sample_slot_playing &&
                 (int32_t)(sample_slot_led_flash_until[i] - now) > 0) {
        LEDS_set(leds, i + 4, LED_BRIGHT);
      } else {
        LEDS_set(leds, i + 4, LED_DIM);
      }
    }
    LEDS_set(leds, KEY_C, LED_BLINK);
    LEDS_set(leds, KEY_D, LED_BRIGHT);
  }

  if (mode_buttons16 == MODE_SAMPLE_SLOT && sample_slot_note_mode)
    LEDS_set(leds, KEY_D, LED_BRIGHT);
  if (mode_buttons16 == MODE_SAMPLE_SLOT && sample_slot_paste) {
    if (sample_slot_paste_arm) {
      static const uint8_t paste_x[16] = {3, 0, 0, 3, 0, 3, 3, 0,
                                           0, 3, 3, 0, 3, 0, 0, 3};
      for (uint8_t i = 0; i < 16; i++) LEDS_set(leds, i + 4, paste_x[i]);
      uint8_t slot = sample_slot_paste == 1 ? sample_slot_edit
                                            : sample_slot_pattern_sel;
      if (slot < 16) LEDS_set(leds, slot + 4, LED_BRIGHT);
    }
    if (sample_slot_paste == 1) {
      LEDS_set(leds, KEY_B, LED_BRIGHT);
      LEDS_set(leds, KEY_D, LED_BRIGHT);
    } else if (sample_slot_paste == 2) {
      LEDS_set(leds, KEY_C, LED_BRIGHT);
      LEDS_set(leds, KEY_D, LED_BRIGHT);
    } else {
      LEDS_set(leds, KEY_D, LED_BLINK);
    }
  }

  if (usb_record_full_flash()) {
    for (uint8_t i = 0; i < 20; i++) LEDS_set(leds, i, LED_BRIGHT);
  }

  LEDS_render(leds);
  return buttons_changed;
}
