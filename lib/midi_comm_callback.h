void midi_comm_callback_fn(uint8_t status, uint8_t channel, uint8_t note,
                           uint8_t velocity) {
  ZV_CALL(if (zv_command(status, channel, note, velocity, time_us_32())) return);
  // printf_sysex("status=%d, channel=%d, note=%d, vel=%d", status, channel,
  // note,
  //              velocity);
  // return;
  // 0x89
  if (!(status == 128 && channel == 9)) {
    return;
  }
  midi_input_activated = true;
  if(!total_number_samples)return;
  if (velocity == 1) {
    if (note == 93) {
      // bpm up
#ifdef INCLUDE_ZEPTOCORE
      if (project_vol < 511) project_vol += 1;
#else
      sf->vol += 1;
#endif
      return;
    } else if (note == 91) {
#ifdef INCLUDE_ZEPTOCORE
      if (project_vol > 0) project_vol -= 1;
#else
      sf->vol -= 1;
#endif
      return;
    } else if (note == 61) {
#ifdef INCLUDE_ZEPTOCORE
      project_set_bpm(project_bpm + 1);
#else
      sf->bpm_tempo += 1;
#endif
      return;
    } else if (note == 45) {
#ifdef INCLUDE_ZEPTOCORE
      project_set_bpm(project_bpm - 1);
#else
      sf->bpm_tempo -= 1;
#endif
      return;
    } else if (note == 39) {
      for (uint8_t i = 1; i < 17; i++) {
        if (banks[(sel_bank_cur + i) % 16]->num_samples > 0) {
          sel_bank_next = (sel_bank_cur + i) % 16;
          sel_sample_next = sel_sample_cur % banks[sel_bank_next]->num_samples;
          fil_current_change = true;
          break;
        }
      }
      return;
    } else if (note == 47) {
      sel_sample_next++;
      if (sel_sample_next >= banks[sel_bank_next]->num_samples) {
        sel_sample_next = 0;
      }
      fil_current_change = true;
      return;
    } else if (note == 112) {
      // stop/play
      if (playback_stopped) {
        cancel_repeating_timer(&timer);
        do_restart_playback = true;
        timer_step();
#ifdef INCLUDE_ZEPTOCORE
        update_repeating_timer_to_bpm(project_bpm);
#else
        update_repeating_timer_to_bpm(sf->bpm_tempo);
#endif

        button_mute = false;
      } else {
        if (!button_mute) trigger_button_mute = true;
        do_stop_playback = true;
      }
    } else if (note == 59) {
      if (button_mute) {
        button_mute = false;
        trigger_button_mute = false;
      } else {
        trigger_button_mute = true;
      }
    }
  } else {
    if (note == 3) {
      printf_sysex(
          "slices=%d",
          banks[sel_bank_cur]->sample[sel_sample_cur].snd[FILEZERO]->slice_num);
    } else if (note == 4) {
      printf_sysex("info=%d,%d,%d,%d,%d,%d", sel_bank_cur, metadata_filename_index(sel_bank_cur,sel_sample_cur),
#ifdef INCLUDE_ZEPTOCORE
                   project_bpm, project_vol,
#else
                   sf->bpm_tempo, sf->vol,
#endif
                   playback_stopped, button_mute);
    }
  }
}
