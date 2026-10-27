#include "start_tempo_settings.h"
#include "project.h"

#ifdef INCLUDE_ZEPTOCORE
#include "midi_receive.h"
#include "midi_channel_settings.h"

static uint8_t boot_ripple_pos;
static volatile uint8_t boot_ripple_laps;
static bool boot_ripple_running;
static struct repeating_timer boot_ripple_timer;

static void boot_ripple_draw(void) {
  if (!leds) return;
  LEDS_clear(leds);
  for (uint8_t y = 0; y < 4; y++) {
    for (uint8_t x = 0; x < 4; x++) {
      int dist = (int)(x + y) - (int)boot_ripple_pos;
      if (dist < 0) dist = -dist;
      uint8_t led = (uint8_t)(16 - y * 4 + x);
      if (dist == 0)
        LEDS_set(leds, led, LED_BRIGHT);
      else if (dist == 1)
        LEDS_set(leds, led, LED_DIM);
    }
  }
  LEDS_render(leds);
}

static bool boot_ripple_timer_cb(struct repeating_timer *t) {
  (void)t;
  uint8_t next = (uint8_t)(boot_ripple_pos + 1);
  if (next > 6) {
    if (boot_ripple_laps < 255) boot_ripple_laps++;
    if (boot_ripple_laps >= 2) {
      boot_ripple_running = false;
      if (leds) {
        LEDS_clear(leds);
        LEDS_render(leds);
      }
      return false;
    }
    next = 0;
  }
  boot_ripple_pos = next;
  boot_ripple_draw();
  return true;
}

static void boot_ripple_start(void) {
  if (!leds) return;
  boot_ripple_pos = 0;
  boot_ripple_laps = 0;
  boot_ripple_running = true;
  boot_ripple_draw();
  add_repeating_timer_ms(-100, boot_ripple_timer_cb, NULL, &boot_ripple_timer);
}

static void boot_ripple_stop(void) {
  while (boot_ripple_running && boot_ripple_laps < 2) sleep_ms(10);
  boot_ripple_running = false;
  cancel_repeating_timer(&boot_ripple_timer);
  if (!leds) return;
  LEDS_clear(leds);
  LEDS_render(leds);
}
void printStringWithDelay2(char *str) {
  int len = strlen(str);
  for (int i = 0; i < len; i++) {
    char currentChar = str[i];
    for (int j = 0; j < sizeof(led_text_5x4) / sizeof(led_text_5x4[0]); j++) {
      if (led_text_5x4[j].character == currentChar) {
        int led = 0;
        for (int row = 0; row < 5; row++) {
          for (int col = 0; col < 4; col++) {
            bool is_one = (led_text_5x4[j].dots[row] >> (3 - col)) & 1;
            if (is_one) {
              LEDS_set(leds, led, LED_BRIGHT);
            } else {
              LEDS_set(leds, led, 0);
            }
            led++;
          }
          printf("\n");
        }
        printf("\n");
        LEDS_render(leds);
        if (currentChar == '.') {
          sleep_ms(50);
        } else {
          sleep_ms(200);
        }
        break;
      }
    }
  }
}
#endif

void flash_no_sdcard() {
  bool started = false;
  while (true) {
#ifdef INCLUDE_ZEPTOCORE
    LEDS_clear(leds);
    printStringWithDelay2("NO SDCARD");
#endif
    sleep_ms(500);
    started = run_mount();
    if (started) {
      // reset
      watchdog_reboot(0, SRAM_END, 0);
    }

#ifdef INCLUDE_ZEPTOCORE
    LEDS_clear(leds);
    printStringWithDelay2("NO SDCARD");
#endif
    sleep_ms(500);
    started = run_mount();
    if (started) {
      // reset
      watchdog_reboot(0, SRAM_END, 0);
    }
  }
}

int extractNumber(const char *str) {
  // Pointer to traverse the string
  const char *p = str;

  // Move the pointer to the end of the string
  while (*p != '\0') {
    p++;
  }

  // Move backwards until we find the first digit
  while (p >= str && !isdigit((unsigned char)*p)) {
    p--;
  }

  // Move backwards to get the entire number
  const char *numStart = p;
  while (numStart >= str && isdigit((unsigned char)*numStart)) {
    numStart--;
  }
  numStart++;  // Move one step forward to the first digit

  // Convert the substring to an integer
  return atoi(numStart);
}

void load_settings(const char *dir_name) {
#ifndef INCLUDE_ZEPTOCORE
  global_start_tempo = start_tempo_load(dir_name, global_start_tempo);
#endif
#ifdef INCLUDE_ZEPTOCORE
  midi_receive_channel = midi_channel_load(dir_name, midi_receive_channel);
#endif
  DIR dj;      /* Directory object */
  FILINFO fno; /* File information */
  FRESULT fr;  /* File result error */
  memset(&dj, 0, sizeof dj);
  memset(&fno, 0, sizeof fno);
  fr = f_findfirst(&dj, &fno, dir_name, "*");
  if (FR_OK != fr) {
    return;
  }
  while (fr == FR_OK && fno.fname[0]) { /* Repeat while an item is found */
    // check for the clock_start_stop_sync
    if (strcmp(fno.fname, "clock_stop_sync-off") == 0) {
      clock_start_stop_sync = false;
    } else if (strcmp(fno.fname, "clock_stop_sync-on") == 0) {
      clock_start_stop_sync = true;
    }

    // check for the mash_mode_momentary
    if (strcmp(fno.fname, "mash_mode_momentary-on") == 0) {
      mode_toggle_momentary = true;
    } else if (strcmp(fno.fname, "mash_mode_momentary-off") == 0) {
      mode_toggle_momentary = false;
    }

    // check for clock output trig/gate
    if (strcmp(fno.fname, "clock_output_trig-on") == 0) {
      clock_output_trig = true;
    } else if (strcmp(fno.fname, "clock_output_trig-off") == 0) {
      clock_output_trig = false;
    }

// switch data.SettingsOverrideWithReset {
//   case "none":
//     os.Create(path.Join(settingsFolder, "override_with_reset-none"))
//   case "sample":
//     os.Create(path.Join(settingsFolder, "override_with_reset-sample"))
//   case "break":
//     os.Create(path.Join(settingsFolder, "override_with_reset-break"))
//   case "amen":
//     os.Create(path.Join(settingsFolder, "override_with_reset-amen"))
//   case "clk":
//     os.Create(path.Join(settingsFolder, "override_with_reset-clk"))
//   }
// check for cv reset override

    // check for clock output behavior (sync with tempo or sync with slice)
    if (strcmp(fno.fname, "clock_behavior_sync_slice-on") == 0) {
      clock_behavior_sync_slice = true;
    } else if (strcmp(fno.fname, "clock_behavior_sync_slice-off") == 0) {
      clock_behavior_sync_slice = false;
    }

    // check for amen cv polarity
    if (strcmp(fno.fname, "amen_cv-unipolar") == 0) {
      global_amen_cv_bipolar = false;
    } else if (strcmp(fno.fname, "amen_cv-bipolar") == 0) {
      global_amen_cv_bipolar = true;
    }

    // check for amen cv behavior
    if (strcmp(fno.fname, "amen_behavior-jump") == 0) {
      global_amen_cv_behavior = AMEN_CV_BEHAVIOR_JUMP;
    } else if (strcmp(fno.fname, "amen_behavior-repeat") == 0) {
      global_amen_cv_behavior = AMEN_CV_BEHAVIOR_REPEAT;
    } else if (strcmp(fno.fname, "amen_behavior-split") == 0) {
      global_amen_cv_behavior = AMEN_CV_BEHAVIOR_SPLIT;
    }

    // check for break cv polarity
    if (strcmp(fno.fname, "break_cv-unipolar") == 0) {
      global_break_cv_bipolar = false;
    } else if (strcmp(fno.fname, "break_cv-bipolar") == 0) {
      global_break_cv_bipolar = true;
    }

    // check for sample cv polarity
    if (strcmp(fno.fname, "sample_cv-unipolar") == 0) {
      global_sample_cv_bipolar = false;
    } else if (strcmp(fno.fname, "sample_cv-bipolar") == 0) {
      global_sample_cv_bipolar = true;
    }

    // check if a file has the prefix "brightness"
    if (strncmp(fno.fname, "brightness-", 11) == 0) {
      global_brightness = extractNumber(fno.fname);
      if (global_brightness > 100) {
        global_brightness = 100;
      }
      printf("[sdcard_startup] '%s' brightness: %d\n", fno.fname,
             global_brightness);
    }

    // check if a file has the prefix "brightness"
    if (strncmp(fno.fname, "negativelatency-", 16) == 0) {
      float negativeLatencyNumber = (float)extractNumber(fno.fname);
      latency_factor = latency_factor * negativeLatencyNumber / 20.0f;
      printf("[sdcard_startup] '%s' negative latency: %d (%3.2f)\n", fno.fname,
             negativeLatencyNumber, latency_factor);
    }

    // check for the clock_start_stop_sync
    if (strcmp(fno.fname, "knobx_select_sample-on") == 0) {
      global_knobx_sample_selector = true;
    } else if (strcmp(fno.fname, "knobx_select_sample-off") == 0) {
      global_knobx_sample_selector = false;
    }

    fr = f_findnext(&dj, &fno); /* Search for next item */
  }
  f_closedir(&dj);
}

void check_setup_files() {
#ifndef INCLUDE_ZEPTOCORE
  DIR dj;      /* Directory object */
  FILINFO fno; /* File information */
  FRESULT fr;  /* File result error */

  // initialize save file
  for (uint8_t i = 0; i < 16; i++) {
    savefile_has_data[i] = false;
  }

  memset(&dj, 0, sizeof dj);
  memset(&fno, 0, sizeof fno);
  fr = f_findfirst(&dj, &fno, "", "*");
  if (FR_OK != fr) {
    return;
  }
  while (fr == FR_OK && fno.fname[0]) { /* Repeat while an item is found */
    // create savefile name
    for (uint8_t i = 0; i < 16; i++) {
      char savefile_name[100];
      sprintf(savefile_name, "savefile%d", i);
      if (strcmp(fno.fname, savefile_name) == 0) {
        printf("[sdcard_startup] found savefile%d\n", i);
        savefile_has_data[i] = true;
      }
    }
    fr = f_findnext(&dj, &fno); /* Search for next item */
  }
  f_closedir(&dj);
#endif

  // load settings
  // sleep_ms(3000);
  // load from base of card (backwards compatibility)
  load_settings("");
  // loading from settings folder (if exists)
  load_settings("settings");
}

void update_reverb() {}

bool filter_was_activated = false;

static int deferred_preset_load=-1, deferred_preset_save=-1;
#ifndef INCLUDE_ZEPTOCORE
static bool savefile_load_state(bool reopen) {
  if(bank_transition_busy()) {deferred_preset_load=savefile_current;return false;}
  if (savefile_has_data[savefile_current]) {
    if(!audio_media_acquire())return false;
    if(!SaveFile_load(sf, savefile_current)) {
      audio_media_release();return false;
    }
    // Boot overrides saved tempo before tempo-dependent effects initialize.
    // Runtime preset recall keeps its ordinary saved tempo.
    if (!reopen)
      sf->bpm_tempo = start_tempo_resolve(global_start_tempo, sf->bpm_tempo);
    if(reopen)audio_file_open(fil_current_name);
    // load new bank and sample
    if(sf->bank<16 && banks[sf->bank] && banks[sf->bank]->num_samples) {
      sel_bank_next = sf->bank;
      int ordinal=metadata_ordinal(sf->bank,sf->sample);
      sel_sample_next = ordinal<0?0:ordinal;
    }
    fil_current_change = true;
    audio_media_release();
    return true;
  }
  return false;
}
void savefile_do_load(void) {
  savefile_load_state(true);
}

bool savefile_do_save(void) {
  if(bank_transition_busy()) {deferred_preset_save=savefile_current;return false;}
  if(!metadata_ready(sel_bank_cur)||!audio_media_acquire())return false;
  sf->bank=sel_bank_cur;
  sf->sample=metadata_filename_index(sel_bank_cur,sel_sample_cur);
  bool saved=SaveFile_save(sf,savefile_current);
  audio_file_open(fil_current_name);
  audio_media_release();
  if(saved)savefile_has_data[savefile_current]=true;
  return saved;
}
#endif

void sdcard_startup() {
  uint32_t diag_startup_us = ZD_TIME();
  ZD_CALL(zeptocore_diag.header[21] = 2);
  for (uint8_t i = SDCARD_CMD_GPIO - 1; i < SDCARD_D0_GPIO + 5; i++) {
    gpio_pull_up(i);
  }

  if (sdcard_startup_is_starting) {
    return;
  }
  sdcard_startup_is_starting = true;
#ifdef INCLUDE_ZEPTOCORE
  boot_ripple_start();
#endif
  if(!audio_media_acquire()) {
#ifdef INCLUDE_ZEPTOCORE
    boot_ripple_stop();
#endif
    sdcard_startup_is_starting=false;
    return;
  }
  fil_is_open = false;
  bool started = false;
  for (uint8_t i = 0; i < 10; i++) {
    started = run_mount();
    if (started) {
      break;
    }
    sleep_ms(10);
  }
  if (!started) {
    printf("[sdcard_startup] could not mount\n");
#ifdef INCLUDE_ZEPTOCORE
    boot_ripple_stop();
#endif
    flash_no_sdcard();
    sleep_ms(1000000);
  }

  // sleep_ms(2000);
  check_setup_files();
  // sleep_ms(2000);

  // load save file
  // load new save file
#ifndef INCLUDE_ZEPTOCORE
  sf = SaveFile_malloc();
  sf->bpm_tempo = start_tempo_resolve(global_start_tempo, sf->bpm_tempo);
  sf->vol = 180;
#else
  project_vol = 180;
#endif

  // sync_using_sdcard = false;
  // SaveFile_save(sf, &sync_using_sdcard);
  // SaveFile_test_sequencer(sf);
  // SaveFile_load(sf);
  // SaveFile_test_sequencer(sf);
  // sleep_ms(3000);


  metadata_catalogue_scan();
  project_boot();
  banks_with_samples_num=0;total_number_samples=0;
  for (unsigned bi=0;bi<16;++bi) {
    if(banks[bi]->num_samples) {
      banks_with_samples[banks_with_samples_num++]=bi;
      total_number_samples+=banks[bi]->num_samples;
      printf("[metadata] bank %u: %u samples, %u detail bytes, slots %04x\n",
          bi+1,banks[bi]->num_samples,(unsigned)banks[bi]->detail_bytes,banks[bi]->valid_slots);
    }
  }
  if(banks_with_samples_num)sel_bank_cur=sel_bank_next=banks_with_samples[0];


#if SEEK_TEST_AUDIO_FIXTURE
  // Explicit test build only: stage a guarded physical variant before the
  // normal detector and map preparation. Requests cannot initiate this work.
  sd_card_t *fixture_card=sd_get_by_num(0);
  FRESULT fixture_result=seek_audio_fixture_run(SEEK_TEST_AUDIO_FIXTURE,fixture_card->state.CID);
  if(fixture_result==FR_DISK_ERR||fixture_result==FR_NOT_READY) {
    f_unmount(fixture_card->pcName);
    if(!run_mount())audio_media_io_failed(FR_NOT_READY);
  }
#endif
  // Audio filenames use one-based bank directories, as playback and the map
  // manifest do. Probe the initial selection using that same convention.
  // if it does, then we are in audio variant mode
  audio_variant_num=0;
  for (uint8_t i = 2; i < 16; i++) {
    char filename[100];
    format_sample_filename(filename,sel_bank_cur,sel_sample_cur,i);
    FILINFO fno;
    FRESULT fr = f_stat(filename, &fno);
    if (fr == FR_OK) {
      audio_variant_num = i;
      printf("[sdcard_startup] audio variant: %s\n", filename);
    }
  }
  if (audio_variant_num > 0) {
    audio_variant_num = (audio_variant_num - 1) / 2;
  }

  // // print transients
  // sleep_ms(2000);
  // printf("transient_num_1: %d\n",
  // banks[0]->sample[0].snd[0]->transient_num_1); for (uint8_t i = 0; i <
  // banks[0]->sample[0].snd[0]->transient_num_1; i++) {
  //   printf("transients[0][%d]: %d\n", i,
  //          banks[0]->sample[0].snd[0]->transients[0][i]);
  // }

  uint32_t total_heap = getTotalHeap();
  uint32_t used_heap = total_heap - getFreeHeap();
  printf("memory usage: %2.1f%% (%ld/%ld)\n",
         (float)(used_heap) / (float)(total_heap) * 100.0, used_heap,
         total_heap);

  // Resolve the saved selection before prioritizing maps or opening playback.
  // Runtime loads retain the normal reopen/fade path through savefile_do_load.
  FRESULT fr;
  format_sample_filename(fil_current_name,sel_bank_cur,sel_sample_cur,
                         sel_variation+audio_variant*2);
  // Keep room for remaining control/filter startup allocations. Optional
  // reverb is constructed only once those allocations have completed.
  bool details_loaded=false;
  void *startup_reserve=malloc(16384);
  bool reserved=startup_reserve && metadata_reserve();
  free(startup_reserve);
  if(reserved && metadata_load_begin(sel_bank_cur,time_us_32())) {
    int result;
    do {
      result=metadata_load_step(time_us_32());
#ifdef INCLUDE_MIDI
      if(tud_inited())tud_task();
#endif
      ZD_CALL(zd_service(ZD_CONTROL));
    } while(result==0);
    if(result>0) {metadata_publish();details_loaded=true;}
    else metadata_load_cancel();
  }


  // Media preparation runs under the boot guard while the audio core services
  // silence. The firmware-owned index is the only new data written to the card.
  sd_card_t *map_card=sd_get_by_num(0);
  fr=details_loaded?seek_maps_prepare(&map_card->state.fatfs,map_card->state.CID,
                      map_card->state.sectors,fil_current_name):FR_NOT_READY;
  if(fr==FR_DISK_ERR) {
    // Discard a potentially dirty FatFs window after an index I/O failure.
    // This boot uses ordinary seeking; a future boot can resume checkpoints.
    seek_maps_unmount();f_unmount(map_card->pcName);
    if(!run_mount())audio_media_io_failed(FR_NOT_READY);
  }
  fr = details_loaded?audio_file_open(fil_current_name):FR_NOT_READY;
  if (fr != FR_OK) {
    printf("[sdcard_startup] could not open %s: %s\n", fil_current_name,
           FRESULT_str(fr));
  }
  phase_new = 0;
  phase_change = true;
  sdcard_startup_is_starting = false;
#ifdef INCLUDE_ZEPTOCORE
  boot_ripple_stop();
  void sample_slot_reserve_check(void);
  sample_slot_reserve_check();
#endif


  audio_media_release();
  time_of_initialization = time_us_64();
  ZD_CALL(zd_metric_record(&zd_control.metrics[ZD_STARTUP],
      time_us_32() - diag_startup_us, 0, 0, false, &zd_control.counters[9]));
  ZD_CALL(zd_control.context[0] = getTotalHeap());
  ZD_CALL(zd_control.context[1] = getFreeHeap());
  ZD_CALL({
    sd_card_t *card = sd_get_by_num(0);
    if (card) {
      memcpy(&zd_control.context[4], card->state.CID, 16);
      zd_control.context[8] = card->state.sectors;
      zd_control.context[9] = card->state.fatfs.fs_type;
      zd_control.context[10] = card->state.fatfs.csize;
      zd_control.context[11] = card->state.fatfs.volbase;
      zd_control.context[12] = card->state.fatfs.fatbase;
      zd_control.context[13] = card->state.fatfs.database;
      zd_control.context[14] = card->state.fatfs.n_fatent;
    }
  });
  ZD_CALL(zeptocore_diag.header[21] = 3);
  ZD_CALL(zd_service(ZD_CONTROL));
}

// Called after all device controls and required startup allocations exist.
static void metadata_optional_reverb(void) {
  metadata_status.minimum_free_heap=getFreeHeap();
  audio_media_boot_complete();
}

static void metadata_deferred_presets(void) {
#ifndef INCLUDE_ZEPTOCORE
  if(bank_transition_busy())return;
  uint8_t current=savefile_current;
  if(deferred_preset_load>=0) {
    savefile_current=deferred_preset_load;deferred_preset_load=-1;
    savefile_do_load();
  } else if(deferred_preset_save>=0) {
    savefile_current=deferred_preset_save;deferred_preset_save=-1;
    savefile_do_save();
  }
  savefile_current=current;
#endif
}
