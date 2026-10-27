// Copyright 2023-2025 Zack Scholl, GPLv3.0
#include "clockhandling.h"
#include "usb_audio.h"
//
#include "midicallback.h"
#include "onewiremidi.h"
#ifdef INCLUDE_MIDI
#include "midi_comm_callback.h"
#endif
#ifdef INCLUDE_SSD1306
#include "image.h"
#include "ssd1306.h"
#endif

void printStringWithDelay(char *str)
{
  int len = strlen(str);
  for (int i = 0; i < len; i++)
  {
    char currentChar = str[i];
    for (int j = 0; j < sizeof(led_text_5x4) / sizeof(led_text_5x4[0]); j++)
    {
      if (led_text_5x4[j].character == currentChar)
      {
        int led = 0;
        for (int row = 0; row < 5; row++)
        {
          for (int col = 0; col < 4; col++)
          {
            bool is_one = (led_text_5x4[j].dots[row] >> (3 - col)) & 1;
            if (is_one)
            {
              LEDS_set(leds, led, LED_BRIGHT);
            }
            else
            {
              LEDS_set(leds, led, 0);
            }
            led++;
          }
        }
        LEDS_render(leds);
        if (currentChar == '.')
        {
          sleep_ms(50);
        }
        else
        {
          sleep_ms(200);
        }
        break;
      }
    }
  }
}

void clear_debouncers()
{
  for (uint8_t i = 0; i < DEBOUNCE_UINT8_NUM; i++)
  {
    DebounceUint8_clear(debouncer_uint8[i]);
  }
  DebounceDigits_clear(debouncer_digits);
}

static float sample_slot_cutoff_hz(int8_t cutoff) {
  int distance = cutoff < 0 ? -cutoff : cutoff;
  if (distance < 1) distance = 1;
  if (distance > 93) distance = 93;
  float t = (distance - 1) / 92.0f;
  return cutoff < 0 ? 8000.0f * powf(0.01f, t) : 80.0f * powf(100.0f, t);
}

static void sample_slot_setup_filter_hz(ResonantFilter *rf, int8_t cutoff,
                                        float hz, uint8_t resonance) {
  if (cutoff == 0) {
    rf->passthrough = true;
    rf->do_setFc = false;
    rf->do_setFilterType = false;
    return;
  }
  float q = 1.5f + (resonance / 127.0f) * 6.5f;
  ResonantFilter_reset2(rf, hz, 44100.0f, q, 0,
                        cutoff < 0 ? FILTER_LOWPASS : FILTER_HIGHPASS);
  rf->passthrough = false;
  rf->filter_was_on = true;
  rf->do_setFc = false;
  rf->do_setFilterType = false;
}

static void sample_slot_refresh_filters(void) {
  static uint32_t last_us;
  uint32_t now = time_us_32();
  uint32_t dt = now - last_us;
  if (dt > 50000) dt = 50000;
  last_us = now;
  float alpha = 1.0f - expf(-(float)dt / 40000.0f);
  for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
    SampleSlotVoice *v = &sample_slot_voice[voice];
    if (!v->filter_dirty && !v->play_filter_glide) continue;
    if (v->play_filter == 0) {
      sample_slot_setup_filter_hz(&v->filter_l, 0, 0, 0);
      sample_slot_setup_filter_hz(&v->filter_r, 0, 0, 0);
      v->play_filter_hz = 0;
      v->play_filter_glide = false;
      v->applied_filter = 0;
      v->applied_filter_q = v->play_filter_q;
      v->filter_dirty = false;
      continue;
    }
    float target = sample_slot_cutoff_hz(v->play_filter);
    bool type_changed = v->applied_filter != 0 &&
                        (v->applied_filter < 0) != (v->play_filter < 0);
    if (!v->play_filter_glide || v->play_filter_hz <= 0.0f || type_changed)
      v->play_filter_hz = target;
    else {
      v->play_filter_hz += (target - v->play_filter_hz) * alpha;
      if (fabsf(v->play_filter_hz - target) <= target * 0.01f)
        v->play_filter_hz = target;
    }
    sample_slot_setup_filter_hz(&v->filter_l, v->play_filter, v->play_filter_hz,
                                v->play_filter_q);
    sample_slot_setup_filter_hz(&v->filter_r, v->play_filter, v->play_filter_hz,
                                v->play_filter_q);
    v->applied_filter = v->play_filter;
    v->applied_filter_q = v->play_filter_q;
    v->play_filter_glide = v->play_filter_hz != target;
    v->filter_dirty = v->play_filter_glide;
  }
}

void __not_in_flash_func(input_handling)()
{
  LEDS_clear(leds);
  LEDS_render(leds);

#ifdef BTN_COL_START
  ButtonMatrix *bm;
  // initialize button matrix
  bm = ButtonMatrix_create(BTN_ROW_START, BTN_COL_START);
#endif

#ifdef INCLUDE_MIDI
  // initialize the midi
  for (uint8_t i = 0; i < 3; i++)
  {
    midiout[i] = MidiOut_malloc(i, false);
  }
  for (uint8_t i = 3; i < MIDIOUTS; i++)
  {
    midiout[i] = MidiOut_malloc(i, true);
  }
#endif

  uint8_t new_vol;
  //   (
  // a=Array.fill(72,{ arg i;
  //   (i+60).midicps.round.asInteger
  // });
  // a.postln;
  // )
  ClockInput *clockinput = NULL;
  Onewiremidi *onewiremidi = NULL;
  if (use_onewiremidi)
  {
    // setup one wire midi
    onewiremidi = Onewiremidi_new(pio0, 3, CLOCK_INPUT_GPIO, midi_note_on,
                                  midi_note_off, midi_start, midi_continue,
                                  midi_stop, midi_timing, midi_control_change);
  }
  else
  {
    clockinput = ClockInput_create(CLOCK_INPUT_GPIO, clock_handling_up,
                                   clock_handling_down, clock_handling_start);
  }

  FilterExp *adcs[3];
  int adc_last[3] = {0, 0, 0};
  int adc_debounce[3] = {0, 0, 0};
  const int adc_threshold_const = 40;
  int adc_threshold = adc_threshold_const;
  const int adc_debounce_max = 25;
  uint16_t adc_startup = 300;
  // TODO add debounce for the adc detection
  for (uint8_t i = 0; i < 3; i++)
  {
    adcs[i] = FilterExp_create(10);
  }

#ifdef INCLUDE_MIDI
  tusb_init();
#endif

#ifdef INCLUDE_SSD1306
  ssd1306_t disp;
  disp.external_vcc = false;
  ssd1306_init(&disp, 128, 64, 0x3C, i2c_default);
  ssd1306_clear(&disp);
#endif

#ifdef DETROITUNDERGROUND
  probability_of_random_jump = 25;
  probability_of_random_retrig = 400;
  probability_of_random_tunnel = 60;
#endif

  bool sel_sample_knob_ready = false;
  clock_start_stop_sync = true;
  ZD_CALL(zd_control.context[2] = getFreeHeap());
  metadata_optional_reverb();
  while (1)
  {
    uint32_t sample_slot_irq = save_and_disable_interrupts();
    uint16_t sample_slot_ticks_due = sample_slot_due;
    sample_slot_due = 0;
    restore_interrupts(sample_slot_irq);
    while (sample_slot_ticks_due--) sample_slot_advance();
    while (!sample_slot_due && sample_slot_prefetch_service()) {
    }
    if (sample_slot_step_mask_pattern != sample_slot_pattern_sel)
      sample_slot_step_mask_refresh();
    if (sample_slot_chain_publish) {
      bool c_up = !key_on_buttons[KEY_C] && !key_did_go_off[KEY_C];
      bool live = sample_slot_playing && sample_slot_chain_entered &&
                  sample_slot_chain_len > 0;
      if (c_up || live) {
      sample_slot_irq = save_and_disable_interrupts();
      uint8_t chain_n = sample_slot_chain_edit_len;
      if (chain_n > 64) chain_n = 64;
      uint8_t first = chain_n > 0 ? sample_slot_chain_edit[0] : 16;
      bool first_empty = true;
      if (chain_n > 0 && first < 16)
        first_empty = !(sample_slot_pattern_mask & (uint16_t)(1u << first));
      bool started = live && !sample_slot_chain_pending &&
                     first < 16 && sample_slot_pattern_sel == first;
      bool defer = live && !started;
      if (!defer && !started && chain_n > 0 && first < 16) {
        sample_slot_pattern_sel = first;
        sample_slot_pattern_next = first;
      }
      if (!defer && !started) {
        sample_slot_chain_pos = 0;
        sample_slot_chain_entered = false;
      }
      if (first_empty) {
        sample_slot_chain_len = 0;
        sample_slot_chain_pending = false;
        if ((defer || started) && first < 16) sample_slot_pattern_next = first;
      } else {
        for (uint8_t i = 0; i < chain_n; i++)
          sample_slot_chain[i] = sample_slot_chain_edit[i];
        sample_slot_chain_len = chain_n;
        if (defer) {
          sample_slot_chain_pending = true;
          sample_slot_pattern_next = first;
        } else if (!started) {
          sample_slot_chain_pending = false;
        }
      }
      sample_slot_chain_publish = false;
      restore_interrupts(sample_slot_irq);
      if (!defer && !started) sample_slot_step_mask_refresh();
      project_mark();
      }
    }
    sample_slot_refresh_filters();
    if (!sample_slot_audio_short()) {
    audio_media_poll();
    metadata_deferred_presets();
    ZD_CALL(zd_service(ZD_CONTROL));
#ifdef INCLUDE_MIDI
    tud_task();
    display_poll(adc_last[0], adc_last[1], adc_last[2]);
    usb_audio_poll();
    usb_record_poll(button_is_pressed(KEY_D));
    if (!sample_slot_chain_hold_save && !sample_slot_voice_using_card())
      project_poll();
    ZV_CALL(zv_realtime_service(tud_mounted(), tud_midi_packet_write));
    // Complete any partially queued telemetry before processing commands that
    // can emit another SysEx message. Clock packets may interleave legally.
    ZV_CALL(if (zv_tx_pending()) {
      zv_service(time_us_32(), tud_mounted(), tud_midi_packet_write);
      continue;
    });
    midi_comm_task(midi_comm_callback_fn, midi_note_on, midi_note_off,
                   midi_start, midi_continue, midi_stop, midi_timing,
                   midi_control_change);
    ZV_CALL(zv_service(time_us_32(), tud_mounted(), tud_midi_packet_write));
    ZV_CALL(if (zv_tx_pending()) continue);
#endif
    }

    if (!total_number_samples && mode_buttons16 != MODE_SAMPLE_SLOT)
    {
      sleep_ms(1);
      continue;
    }
    if (do_switch_between_clock_and_midi)
    {
      do_switch_between_clock_and_midi = false;
      if (use_onewiremidi)
      {
        // TODO: switching back doesn't work yet
        // // switch to clock
        // Onewiremidi_destroy(onewiremidi);
        // clockinput =
        //     ClockInput_create(CLOCK_INPUT_GPIO, clock_handling_up,
        //                       clock_handling_down, clock_handling_start);
      }
      else
      {
        // switch to one wire midi
        ClockInput_destroy(clockinput);
        onewiremidi = Onewiremidi_new(
            pio0, 3, CLOCK_INPUT_GPIO, midi_note_on, midi_note_off, midi_start,
            midi_continue, midi_stop, midi_timing, midi_control_change);
      }
      use_onewiremidi = !use_onewiremidi;
    }

    // if in startup deduct
    if (adc_startup > 0)
    {
      adc_startup--;
      // if (adc_startup == 0) {
      //   for (int i = 1; i < 2; i++) {
      //     PIO p = (i == 0) ? pio0 : pio1;
      //     printf("PIO%d:\n", i);
      //     for (int sm = 2; sm < 4; sm++) {
      //       if (pio_sm_is_claimed(p, sm)) {
      //         printf("  State Machine %d: USED\n", sm);
      //       } else {
      //         printf("  State Machine %d: NOT USED\n", sm);
      //       }
      //     }
      //     sleep_ms(1);
      //   }
      // }
    }

    // check for input
    int char_input = getchar_timeout_us(10);
    if (char_input >= 0)
    {
      if (char_input == 118)
      {
        send_text_as_sysex("version=v8.0.4");
      }
    }

#ifdef BTN_COL_START
    // button handler
    if (!sample_slot_audio_short() && button_handler(bm))
    {
      // reset knob debouncers
      adc_threshold = 10000;
      for (uint8_t i = 0; i < 3; i++)
      {
        adc_debounce[i] = 0;
      }
    }
    else
    {
      adc_threshold = adc_threshold_const;
    }
#endif

#ifdef INCLUDE_CLOCKINPUT
    if (!use_onewiremidi)
    {
      // clock input handler
      ClockInput_update(clockinput);
      if (clock_in_do)
      {
        clock_input_absent_zeptocore =
            ClockInput_timeSinceLast(clockinput) > 1000000;
      }
      if (!clock_start_stop_sync && clock_in_do)
      {
        if ((time_us_32() - clock_in_last_time) > 2 * clock_in_diff_2x)
        {
          clock_in_ready = false;
          clock_in_do = false;
          clock_in_activator = 0;
        }
      }
    }
    else
    {
      Onewiremidi_receive(onewiremidi);
    }
#endif
    if (sample_slot_audio_short()) continue;

#ifdef INCLUDE_SSD1306
    char buf_ssd1306[16];
    format_int32_decimal(buf_ssd1306, beat_current);
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 8, 24, 1, buf_ssd1306);
    ssd1306_bmp_show_image_with_offset(&disp, output_bmp_data, output_bmp_size,
                                       0, 64 - 17);
    // tile 4x4 on the right 64x64 pixels of a 14x14 image
    for (int i = 0; i < 4; i++)
    {
      for (int j = 0; j < 4; j++)
      {
        ssd1306_bmp_show_image_with_offset(&disp, sun_solid2_bmp_data,
                                           sun_solid2_bmp_size, 64 + i * 16,
                                           64 - 17 - j * 16 + 1);
      }
    }
    ssd1306_show(&disp);
#endif

    adc_select_input(2);

    // check if a single button is held
    // for purposes of changing the fx params
    int8_t single_key = -1;
    if (key_on_buttons[0] > 0 && key_on_buttons[1] > 0)
    {
    }
    else
    {
      for (uint8_t i = 4; i < 20; i++)
      {
        if (key_on_buttons[i] > 0)
        {
          if (single_key == -1)
          {
            single_key = i;
          }
          else
          {
            single_key = -1;
            break;
          }
        }
      }
    }
    if (single_key > -1)
    {
      DebounceDigits_clear(debouncer_digits);
    }

    int adc;

#ifdef INCLUDE_KNOBS
    // knob X
    static bool sample_slot_x_pitch_lock = false;
    static uint16_t sample_slot_env_wait = 0;
    static bool sample_slot_env_held[3] = {false, false, false};
    static int sample_slot_env_raw[3] = {0, 0, 0};
    static uint8_t sample_slot_env_streak[3] = {0, 0, 0};
    static uint8_t sample_slot_env_arm[3] = {0, 0, 0};
    static uint8_t sample_slot_env_wait_key = 0;
    static uint16_t sample_slot_x_settle = 0;
    static int sample_slot_x_anchor = 0;
    uint16_t adc_raw = adc_read();
    adc = FilterExp_update(adcs[0], adc_raw);
    bool x_mod = button_is_pressed(KEY_B) || button_is_pressed(KEY_C) ||
                 button_is_pressed(KEY_D);
    if (sample_slot_x_pitch_lock && !x_mod) {
      if (abs(sample_slot_x_anchor - adc) > adc_threshold) {
        sample_slot_x_settle = 250;
        sample_slot_x_anchor = adc;
      }
      adc_last[0] = adc;
      if (sample_slot_x_settle > 0)
        sample_slot_x_settle--;
      else
        sample_slot_x_pitch_lock = false;
    } else if (sample_slot_x_pitch_lock && x_mod) {
      sample_slot_x_settle = 250;
      sample_slot_x_anchor = adc;
    }
    if (sample_slot_env_held[0] &&
        button_is_pressed(sample_slot_env_arm[0])) {
      if (abs(sample_slot_env_raw[0] - (int)adc_raw) > adc_threshold) {
        if (++sample_slot_env_streak[0] >= 4)
          sample_slot_env_held[0] = false;
        else
          adc_last[0] = adc;
      } else {
        sample_slot_env_streak[0] = 0;
        adc_last[0] = adc;
      }
    }
    if (abs(adc_last[0] - adc) > adc_threshold)
    {
      adc_debounce[0] = adc_debounce_max;
    }
    if (adc_debounce[0])
    {
      adc_last[0] = adc;
      adc_debounce[0]--;
      if (adc_startup == 0)
      {
        if (mode_buttons16 == MODE_SAMPLE_SLOT && single_key_on >= 4 &&
            single_key_on < 20) {
          SampleSlotNote *note =
              sample_slot_note_at(sample_slot_edit, single_key_on - 4);
          if (note) {
            int nudge = adc * (SAMPLE_SLOT_TICKS_PER_STEP / 2) / 4095;
            if (nudge < 0) nudge = 0;
            if (nudge > SAMPLE_SLOT_TICKS_PER_STEP / 2)
              nudge = SAMPLE_SLOT_TICKS_PER_STEP / 2;
            note->nudge = (uint8_t)nudge;
            sample_slot_hold_edited = true;
          }
        } else if (button_is_pressed(KEY_A))
        {
          if (mode_buttons16 == MODE_SAMPLE_SLOT)
            sample_slot_a_play_candidate = false;
#ifdef INCLUDE_MIDI
          // send out midi cc
          MidiOut_cc(midiout[0], cc_volume, adc * 127 / 4096);
#endif
          new_vol = adc * VOLUME_STEPS / 4096;
          // new_vol = 100;
          if (new_vol != project_vol)
          {
            project_vol = new_vol;
            if (mode_buttons16 == MODE_SAMPLE_SLOT) project_mark();
          }
          clear_debouncers();
          DebounceUint8_set(debouncer_uint8[DEBOUNCE_UINT8_LED_WALL],
                            adc * 255 / 4096, 200);
        }
        else if (button_is_pressed(KEY_B))
        {
          if (mode_buttons16 == MODE_SAMPLE_SLOT) {
            sample_slot_x_pitch_lock = true;
            const int dead = 200;
            bool slice = sample_slot_slice_mode[sample_slot_edit];
            if (!slice && adc > 2048 + dead) {
              for (uint8_t i = 0; i < 16; i++) {
                sample_slot_slice_custom[sample_slot_edit][i] = false;
                sample_slot_slice_end_on[sample_slot_edit][i] = false;
              }
              sample_slot_slice_mode[sample_slot_edit] = true;
              project_mark();
              if (ledtext) {
                ledtext->i = 0;
                LEDText_display(ledtext, "S");
              }
            } else if (slice && adc < 2048 - dead) {
              sample_slot_slice_mode[sample_slot_edit] = false;
              project_mark();
              if (ledtext) {
                ledtext->i = 0;
                LEDText_display(ledtext, "C");
              }
            }
          }
        }
        else if (button_is_pressed(KEY_C) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          sample_slot_x_pitch_lock = true;
          sample_slot_attack[sample_slot_edit] =
              (uint16_t)((uint32_t)adc * SAMPLE_SLOT_ENV_MAX / 4095u);
          project_mark();
          if (adc_debounce[0] == 0) {
            if (!sample_slot_env_held[0])
              sample_slot_env_wait = 8;
            sample_slot_env_held[0] = true;
            sample_slot_env_raw[0] = adc_raw;
            sample_slot_env_streak[0] = 0;
            sample_slot_env_arm[0] = KEY_C;
            sample_slot_env_wait_key = KEY_C;
          } else {
            sample_slot_env_wait = 0;
          }
        }
        else if (button_is_pressed(KEY_D) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          sample_slot_note_d_alone_candidate = false;
          sample_slot_x_pitch_lock = true;
          int level = 1 + adc * 126 / 4095;
          if (level < 1) level = 1;
          if (level > 127) level = 127;
          sample_slot_level[sample_slot_edit] = (uint8_t)level;
          for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
            if (sample_slot_voice[voice].sample == sample_slot_edit)
              sample_slot_voice[voice].play_velocity = (uint8_t)level;
          }
          project_mark();
          if (adc_debounce[0] == 0) {
            if (!sample_slot_env_held[0])
              sample_slot_env_wait = 8;
            sample_slot_env_held[0] = true;
            sample_slot_env_raw[0] = adc_raw;
            sample_slot_env_streak[0] = 0;
            sample_slot_env_arm[0] = KEY_D;
            sample_slot_env_wait_key = KEY_D;
          } else {
            sample_slot_env_wait = 0;
          }
        }
        else if (mode_buttons16 == MODE_SAMPLE_SLOT && single_key_on < 4 &&
                 !sample_slot_x_pitch_lock)
        {
          const int dead = 200;
          int offset = 0;
          if (adc < 2048 - dead)
            offset = -((2048 - dead - adc) * 12 / (2048 - dead));
          else if (adc > 2048 + dead)
            offset = (adc - (2048 + dead)) * 12 / (2048 - dead);
          if (offset < -12) offset = -12;
          if (offset > 12) offset = 12;
          sample_slot_pitch[sample_slot_edit] = (int8_t)offset;
          sample_slot_active_pitch[sample_slot_edit] = (int8_t)offset;
          project_mark();
          if (!sample_slot_playing) {
            for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
              if (sample_slot_voice[voice].sample == sample_slot_edit)
                sample_slot_voice[voice].play_pitch = (int8_t)offset;
            }
          }
        }
        else
        {
#ifdef INCLUDE_MIDI
          // send out midi cc
          MidiOut_cc(midiout[0], cc_knobx, adc * 127 / 4096);
#endif
        }
      }
    }
#endif

#ifdef BTN_COL_START
    if (!is_arcade_box)
      button_handler(bm);
#endif

#ifdef INCLUDE_CLOCKINPUT
    if (!use_onewiremidi)
    {
      // clock input handler
      ClockInput_update(clockinput);
      if (clock_in_do)
      {
        clock_input_absent_zeptocore =
            ClockInput_timeSinceLast(clockinput) > 1000000;
      }
      if (!clock_start_stop_sync && clock_in_do)
      {
        if ((time_us_32() - clock_in_last_time) > 2 * clock_in_diff_2x)
        {
          clock_in_ready = false;
          clock_in_do = false;
          clock_in_activator = 0;
        }
      }
    }
    else
    {
      Onewiremidi_receive(onewiremidi);
    }
#endif

#ifdef INCLUDE_KNOBS
    // knob Y
    adc_select_input(1);
    uint16_t y_raw = adc_read();
    adc = FilterExp_update(adcs[1], y_raw);
    static bool sample_slot_y_lock = false;
    static uint16_t sample_slot_y_settle = 0;
    static int sample_slot_y_anchor = 0;
    bool y_mod = button_is_pressed(KEY_A) || button_is_pressed(KEY_B) ||
                 button_is_pressed(KEY_C) || button_is_pressed(KEY_D);
    if (sample_slot_y_lock && !y_mod) {
      if (abs(sample_slot_y_anchor - adc) > adc_threshold) {
        sample_slot_y_settle = 250;
        sample_slot_y_anchor = adc;
      }
      adc_last[1] = adc;
      if (sample_slot_y_settle > 0)
        sample_slot_y_settle--;
      else
        sample_slot_y_lock = false;
    } else if (sample_slot_y_lock) {
      sample_slot_y_settle = 250;
      sample_slot_y_anchor = adc;
    }
    if (sample_slot_env_held[1] &&
        button_is_pressed(sample_slot_env_arm[1])) {
      if (abs(sample_slot_env_raw[1] - (int)y_raw) > adc_threshold) {
        if (++sample_slot_env_streak[1] >= 4)
          sample_slot_env_held[1] = false;
        else
          adc_last[1] = adc;
      } else {
        sample_slot_env_streak[1] = 0;
        adc_last[1] = adc;
      }
    }
    if (abs(adc_last[1] - adc) > adc_threshold)
    {
      adc_debounce[1] = adc_debounce_max;
    }
    if (adc_debounce[1] > 0)
    {
      adc_last[1] = adc;
      adc_debounce[1]--;
      if (adc_startup == 0)
      {
        if (mode_buttons16 == MODE_SAMPLE_SLOT && single_key_on >= 4 &&
            single_key_on < 20) {
          SampleSlotNote *note =
              sample_slot_note_at(sample_slot_edit, single_key_on - 4);
          if (note) {
            int velocity = 1 + adc * 126 / 4095;
            if (velocity < 1) velocity = 1;
            if (velocity > 127) velocity = 127;
            note->velocity = (uint8_t)velocity;
            sample_slot_hold_edited = true;
          }
        } else if (button_is_pressed(KEY_A))
        {
          if (mode_buttons16 == MODE_SAMPLE_SLOT) {
            sample_slot_a_play_candidate = false;
            sample_slot_y_lock = true;
            usb_audio_level = adc;
            DebounceUint8_set(debouncer_uint8[DEBOUNCE_UINT8_LED_BAR],
                              adc * 255 / 4096, 200);
          }
        }
        else if (button_is_pressed(KEY_B) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          const int dead = 500;
          const int span = 2048 - dead;
          int8_t cutoff = 0;
          if (adc < 2048 - dead) {
            int fc = adc * 93 / span;
            if (fc > 92) fc = 92;
            cutoff = (int8_t)(fc - 93);
          } else if (adc > 2048 + dead) {
            int fc = (adc - (2048 + dead)) * 93 / span;
            if (fc > 92) fc = 92;
            cutoff = (int8_t)(fc + 1);
          }
          sample_slot_filter[sample_slot_edit] = cutoff;
          sample_slot_y_lock = true;
          project_mark();
          if (!sample_slot_playing) {
            for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
              SampleSlotVoice *playing = &sample_slot_voice[voice];
              if (playing->sample == sample_slot_edit) {
                playing->play_filter = cutoff;
                playing->play_filter_glide = true;
                playing->filter_dirty = true;
              }
            }
            if (adc_debounce[1] == 0) {
              if (!sample_slot_env_held[1]) {
                sample_slot_env_wait = 8;
                sample_slot_env_wait_key = KEY_B;
              }
              sample_slot_env_held[1] = true;
              sample_slot_env_raw[1] = y_raw;
              sample_slot_env_streak[1] = 0;
              sample_slot_env_arm[1] = KEY_B;
            } else {
              sample_slot_env_wait = 0;
            }
          }
        }
        else if (button_is_pressed(KEY_C) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          sample_slot_y_lock = true;
          if (adc >= 4095 - 200)
            sample_slot_decay[sample_slot_edit] = 0;
          else
          sample_slot_decay[sample_slot_edit] = (uint16_t)(
              SAMPLE_SLOT_ENV_MIN +
              (uint32_t)adc * (SAMPLE_SLOT_ENV_MAX - SAMPLE_SLOT_ENV_MIN) /
                  (4095u - 200u));
          project_mark();
          if (adc_debounce[1] == 0) {
            if (!sample_slot_env_held[1])
              sample_slot_env_wait = 8;
            sample_slot_env_held[1] = true;
            sample_slot_env_raw[1] = y_raw;
            sample_slot_env_streak[1] = 0;
            sample_slot_env_arm[1] = KEY_C;
            sample_slot_env_wait_key = KEY_C;
          } else {
            sample_slot_env_wait = 0;
          }
        }
        else if (button_is_pressed(KEY_D) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          sample_slot_note_d_alone_candidate = false;
          sample_slot_y_lock = true;
          sample_slot_fine_bound(sample_slot_edit, false, (uint16_t)adc);
          if (adc_debounce[1] == 0 && !sample_slot_playing)
            sample_slot_preview(sample_slot_edit,
                                sample_slot_slice_sel[sample_slot_edit], true);
        }
        else if (mode_buttons16 == MODE_SAMPLE_SLOT && single_key_on < 4 &&
                 !sample_slot_y_lock)
        {
          sample_slot_set_bound(sample_slot_edit, false, (uint16_t)adc);
          if (adc_debounce[1] == 0 &&
              sample_slot_slice_mode[sample_slot_edit])
            sample_slot_preview(sample_slot_edit,
                                sample_slot_slice_sel[sample_slot_edit], true);
        }
        else
        {
#ifdef INCLUDE_MIDI
          // send out midi cc
          MidiOut_cc(midiout[0], cc_knoby, adc * 127 / 4096);
#endif
        }
      }
    }
#endif

#ifdef INCLUDE_CLOCKINPUT
    if (!use_onewiremidi)
    {
      // clock input handler
      ClockInput_update(clockinput);
      if (clock_in_do)
      {
        clock_input_absent_zeptocore =
            ClockInput_timeSinceLast(clockinput) > 1000000;
      }
      if (!clock_start_stop_sync && clock_in_do)
      {
        if ((time_us_32() - clock_in_last_time) > 2 * clock_in_diff_2x)
        {
          clock_in_ready = false;
          clock_in_do = false;
          clock_in_activator = 0;
        }
      }
    }
    else
    {
      Onewiremidi_receive(onewiremidi);
    }
#endif

#ifdef BTN_COL_START
    if (!is_arcade_box)
      button_handler(bm);
#endif

#ifdef INCLUDE_CLOCKINPUT
    if (!use_onewiremidi)
    {
      // clock input handler
      ClockInput_update(clockinput);
      if (clock_in_do)
      {
        clock_input_absent_zeptocore =
            ClockInput_timeSinceLast(clockinput) > 1000000;
      }
      if (!clock_start_stop_sync && clock_in_do)
      {
        if ((time_us_32() - clock_in_last_time) > 2 * clock_in_diff_2x)
        {
          clock_in_ready = false;
          clock_in_do = false;
          clock_in_activator = 0;
        }
      }
    }
    else
    {
      Onewiremidi_receive(onewiremidi);
    }
#endif

#ifdef INCLUDE_KNOBS
    // knob Z
    adc_select_input(0);
    uint16_t z_raw = adc_read();
    adc = FilterExp_update(adcs[2], z_raw);
    static bool sample_slot_z_lock = false;
    static uint16_t sample_slot_z_settle = 0;
    static int sample_slot_z_anchor = 0;
    bool z_mod = button_is_pressed(KEY_A) || button_is_pressed(KEY_B) ||
                 button_is_pressed(KEY_C) || button_is_pressed(KEY_D);
    if (sample_slot_z_lock && !z_mod) {
      if (abs(sample_slot_z_anchor - adc) > adc_threshold) {
        sample_slot_z_settle = 250;
        sample_slot_z_anchor = adc;
      }
      adc_last[2] = adc;
      if (sample_slot_z_settle > 0)
        sample_slot_z_settle--;
      else
        sample_slot_z_lock = false;
    } else if (sample_slot_z_lock) {
      sample_slot_z_settle = 250;
      sample_slot_z_anchor = adc;
    }
    if (sample_slot_env_held[2] &&
        button_is_pressed(sample_slot_env_arm[2])) {
      if (abs(sample_slot_env_raw[2] - (int)z_raw) > adc_threshold) {
        if (++sample_slot_env_streak[2] >= 4)
          sample_slot_env_held[2] = false;
        else
          adc_last[2] = adc;
      } else {
        sample_slot_env_streak[2] = 0;
        adc_last[2] = adc;
      }
    }
    if (abs(adc_last[2] - adc) > adc_threshold)
    {
      adc_debounce[2] = adc_debounce_max;
    }
    if (adc_debounce[2] > 0)
    {
      adc_last[2] = adc;
      adc_debounce[2]--;
      if (adc_startup == 0)
      {
        if (button_is_pressed(KEY_A))
        {
          if (mode_buttons16 == MODE_SAMPLE_SLOT) {
            sample_slot_a_play_candidate = false;
            sample_slot_z_lock = true;
            uint16_t bpm = (uint16_t)(60 + adc * 120 / 4095);
            if (bpm != project_get_bpm()) {
              project_set_bpm(bpm);
              update_repeating_timer_to_bpm(bpm);
              project_mark();
            }
          }
        }
        else if (button_is_pressed(KEY_B) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          uint8_t resonance = adc * 127 / 4095;
          sample_slot_z_lock = true;
          sample_slot_filter_q[sample_slot_edit] = resonance;
          project_mark();
          if (!sample_slot_playing) {
            for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
              SampleSlotVoice *playing = &sample_slot_voice[voice];
              if (playing->sample == sample_slot_edit) {
                playing->play_filter_q = resonance;
                playing->filter_dirty = true;
              }
            }
            if (adc_debounce[2] == 0) {
              if (!sample_slot_env_held[2]) {
                sample_slot_env_wait = 8;
                sample_slot_env_wait_key = KEY_B;
              }
              sample_slot_env_held[2] = true;
              sample_slot_env_raw[2] = z_raw;
              sample_slot_env_streak[2] = 0;
              sample_slot_env_arm[2] = KEY_B;
            } else {
              sample_slot_env_wait = 0;
            }
          }
        }
        else if (button_is_pressed(KEY_C) && mode_buttons16 == MODE_SAMPLE_SLOT)
        {
          sample_slot_z_lock = true;
          sample_slot_release[sample_slot_edit] = (uint16_t)(
              SAMPLE_SLOT_ENV_MIN +
              (uint32_t)adc * (SAMPLE_SLOT_ENV_MAX - SAMPLE_SLOT_ENV_MIN) / 4095u);
          project_mark();
          if (adc_debounce[2] == 0) {
            if (!sample_slot_env_held[2])
              sample_slot_env_wait = 8;
            sample_slot_env_held[2] = true;
            sample_slot_env_raw[2] = z_raw;
            sample_slot_env_streak[2] = 0;
            sample_slot_env_arm[2] = KEY_C;
            sample_slot_env_wait_key = KEY_C;
          } else {
            sample_slot_env_wait = 0;
          }
        }
        else if (button_is_pressed(KEY_D))
        {
          if (mode_buttons16 == MODE_SAMPLE_SLOT) {
            sample_slot_note_d_alone_candidate = false;
            sample_slot_z_lock = true;
            sample_slot_fine_bound(sample_slot_edit, true, (uint16_t)adc);
            if (adc_debounce[2] == 0 && !sample_slot_playing)
              sample_slot_preview_end(sample_slot_edit);
          }
        }
        else if (mode_buttons16 == MODE_SAMPLE_SLOT && single_key_on >= 4 &&
                 single_key_on < 20)
        {
          SampleSlotNote *note =
              sample_slot_note_at(sample_slot_edit, single_key_on - 4);
          if (note) {
            note->offset = adc * 255 / 4095;
            sample_slot_hold_edited = true;
          }
        }
        else if (mode_buttons16 == MODE_SAMPLE_SLOT && single_key_on < 4 &&
                 !sample_slot_z_lock)
        {
          sample_slot_set_bound(sample_slot_edit, true, (uint16_t)adc);
        }
        else
        {
#ifdef INCLUDE_MIDI
          // send out midi cc
          MidiOut_cc(midiout[0], cc_knobz, adc * 127 / 4096);
#endif
        }
      }
    }
    for (uint8_t knob = 0; knob < 3; knob++) {
      if (sample_slot_env_held[knob] &&
          !button_is_pressed(sample_slot_env_arm[knob])) {
        sample_slot_env_held[knob] = false;
        sample_slot_env_streak[knob] = 0;
      }
    }
    if (sample_slot_env_wait > 0 &&
        !button_is_pressed(sample_slot_env_wait_key))
      sample_slot_env_wait = 0;
    else if (sample_slot_env_wait > 0) {
      if (sample_slot_playing)
        sample_slot_env_wait = 0;
      else if (--sample_slot_env_wait == 0) {
        sample_slot_preview(sample_slot_edit,
                            sample_slot_slice_sel[sample_slot_edit], true);
        if (sample_slot_env_wait_key == KEY_C ||
            sample_slot_env_wait_key == KEY_D) {
          for (uint8_t voice = 0; voice < SAMPLE_SLOT_VOICES; voice++) {
            if (sample_slot_voice[voice].sample == sample_slot_edit)
              sample_slot_voice[voice].attack_len =
                  sample_slot_attack[sample_slot_edit];
          }
        }
      }
    }
#endif

    // TODO: dead code?
    // update the text if any
    LEDText_update(ledtext, leds);
    // TODO: redundant code?
    LEDS_render(leds);

#ifdef BTN_COL_START
    if (!is_arcade_box)
      button_handler(bm);
#endif

#ifdef INCLUDE_CLOCKINPUT
    if (!use_onewiremidi)
    {
      // clock input handler
      ClockInput_update(clockinput);
      if (clock_in_do)
      {
        clock_input_absent_zeptocore =
            ClockInput_timeSinceLast(clockinput) > 1000000;
      }
      if (!clock_start_stop_sync && clock_in_do)
      {
        if ((time_us_32() - clock_in_last_time) > 2 * clock_in_diff_2x)
        {
          clock_in_ready = false;
          clock_in_do = false;
          clock_in_activator = 0;
        }
      }
    }
    else
    {
      Onewiremidi_receive(onewiremidi);
    }
#endif

#ifdef INCLUDE_CLOCKINPUT
    if (!use_onewiremidi)
    {
      // clock input handler
      ClockInput_update(clockinput);
      if (clock_in_do)
      {
        clock_input_absent_zeptocore =
            ClockInput_timeSinceLast(clockinput) > 1000000;
      }
      if (!clock_start_stop_sync && clock_in_do)
      {
        if ((time_us_32() - clock_in_last_time) > 2 * clock_in_diff_2x)
        {
          clock_in_ready = false;
          clock_in_do = false;
          clock_in_activator = 0;
        }
      }
    }
    else
    {
      Onewiremidi_receive(onewiremidi);
    }
#endif

#ifdef INCLUDE_KEYBOARD
    // check keyboard
    run_keyboard();
#endif

  }
}

static void sample_slot_resample_reset_voices(void) {
  for (uint8_t i = 0; i < SAMPLE_SLOT_VOICES; i++) {
    SampleSlotVoice *v = &sample_slot_voice[i];
    sample_slot_voice_close(v);
    v->restart = false;
    v->defer_restart = false;
    v->steal_after = false;
    v->kill = false;
    v->seek = false;
    v->release_left = 0;
    v->release_len = 0;
    v->open = false;
    v->cue = false;
  }
}

void sample_slot_resample_run(uint8_t slot) {
  if (slot > 15 || sample_slot_playing || sample_slot_resample_busy) return;
  if (sample_slot_ord(slot) >= 0) return;
  if (sample_slot_chain_edit_len > 0) {
    uint8_t chain_n = sample_slot_chain_edit_len;
    if (chain_n > 64) chain_n = 64;
    for (uint8_t i = 0; i < chain_n; i++)
      sample_slot_chain[i] = sample_slot_chain_edit[i];
    sample_slot_chain_len = chain_n;
    sample_slot_chain_pos = 0;
    project_mark();
  }
  if (sample_slot_chain_len == 0 || sample_slot_chain[0] > 15) return;
  if (!sample_slot_resample_open(slot)) {
    sample_slot_reserve_deny();
    sample_slot_resample_pick = false;
    return;
  }
  sample_slot_resample_busy = true;
  __dmb();
  sleep_ms(50);
  float bpm = project_get_bpm();
  uint32_t tick_us = (uint32_t)(314441.0f / bpm + 0.5f);
  if (tick_us == 0) tick_us = 1;
  uint64_t expect = (uint64_t)sample_slot_chain_len * 16ull * 48ull * tick_us *
                    44100ull / 1000000ull;
  if (expect > 20ull * 44100ull) expect = 20ull * 44100ull;
  if (expect < 1) expect = 1;
  const uint32_t limit = 20u * 44100u;
  uint32_t produced = 0;
  bool ok = true;
  sample_slot_resample_solo = 0xff;
  sample_slot_resample_reset_voices();
  sample_slot_pattern_sel = sample_slot_chain[0];
  sample_slot_pattern_next = sample_slot_chain[0];
  sample_slot_chain_pos = 0;
  sample_slot_chain_entered = true;
  sample_slot_chain_pending = false;
  sample_slot_ticks = 0;
  sample_slot_step = 0;
  sample_slot_step_mask_refresh();
  sample_slot_fire_step(0, false);
  sample_slot_refresh_filters();
  for (uint16_t spin = 0; spin < 400; spin++) {
    bool waiting = false;
    for (uint8_t vi = 0; vi < SAMPLE_SLOT_VOICES; vi++) {
      SampleSlotVoice *v = &sample_slot_voice[vi];
      if (v->restart || v->seek) waiting = true;
    }
    if (!waiting || !sample_slot_prefetch_service()) break;
  }
  uint64_t acc = 0;
  uint8_t out[512];
  uint32_t fill = 0;
  uint8_t patterns_left = sample_slot_chain_len;
  while (ok && produced < limit) {
    sample_slot_refresh_filters();
    acc += 44100ull * tick_us;
    uint32_t n = (uint32_t)(acc / 1000000ull);
    acc %= 1000000ull;
    while (n && ok && produced < limit) {
      uint32_t chunk = n > 128u ? 128u : n;
      int32_t *mix = sample_slot_resample_mix(chunk);
      for (uint32_t i = 0; i < chunk; i++) {
        int32_t left = mix[i * 2] >> 16;
        int32_t right = mix[i * 2 + 1] >> 16;
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        int32_t mixed = (left + right) / 2;
        if (mixed > 32767) mixed = 32767;
        if (mixed < -32768) mixed = -32768;
        int16_t s = (int16_t)mixed;
        out[fill++] = (uint8_t)s;
        out[fill++] = (uint8_t)((uint16_t)s >> 8);
        if (fill == sizeof out) {
          ok = sample_slot_resample_write(out, fill);
          fill = 0;
        }
        produced++;
      }
      n -= chunk;
    }
    uint32_t lit = (uint32_t)(((uint64_t)produced * 16u) / expect);
    if (lit > 16) lit = 16;
    sample_slot_copy_bar((uint8_t)lit);
    if (produced >= limit) break;
    bool end_step = sample_slot_step == 15 && sample_slot_ticks == 47;
    if (end_step) {
      if (patterns_left <= 1) break;
      patterns_left--;
    }
    sample_slot_advance();
  }
  if (ok && fill) ok = sample_slot_resample_write(out, fill);
  sample_slot_resample_solo = 0xff;
  sample_slot_resample_commit(ok && produced > 0);
  sample_slot_resample_reset_voices();
  sample_slot_resample_busy = false;
  sample_slot_resample_pick = false;
  uint32_t irq = save_and_disable_interrupts();
  sample_slot_due = 0;
  restore_interrupts(irq);
  sample_slot_copy_bar(0);
}
