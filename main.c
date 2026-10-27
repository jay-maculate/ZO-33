// Copyright 2023-2025 Zack Scholl, GPLv3.0

#include "lib/includes.h"

// static uint8_t dub_step_numerator[] = {1, 1, 1, 1, 1, 1, 1, 1};
// static uint8_t dub_step_denominator[] = {2, 3, 4, 8, 8, 12, 12, 16};
// static uint8_t dub_step_steps[] = {8, 12, 16, 32, 16, 16};
bool repeating_timer_callback_playback_stopped = false;
uint64_t repeating_timer_callback_playback_counter = 0;
int32_t phase_sample_old = 0;
// timer


bool __not_in_flash_func(timer_step)() {
  if (mode_buttons16 == MODE_SAMPLE_SLOT && sample_slot_due < 64)
    sample_slot_due++;
  return true;
}

bool __not_in_flash_func(repeating_timer_callback)(struct repeating_timer *t) {
  return timer_step();
}

#ifdef INCLUDE_ZEPTOCORE
#include "lib/zeptocore.h"
#endif

int main() {
  // Set PLL_USB 96MHz
  const uint32_t main_line = 96;
  pll_init(pll_usb, 1, main_line * 16 * MHZ, 4, 4);
  clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                  main_line * MHZ, main_line / 2 * MHZ);
  // Change clk_sys to be 96MHz.
  clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                  CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                  main_line * MHZ, main_line * MHZ);
  // CLK peri is clocked from clk_sys so need to change clk_peri's freq
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  main_line * MHZ, main_line * MHZ);
  // Reinit uart now that clk_peri has changed
  stdio_init_all();
  // overclocking!!!
  // note that overclocking >200Mhz requires setting sd_card_sdio
  // rp2040_sdio_init(sd_card_t, 2);
// otherwise clock divider of 1 is fine
// set_sys_clock_khz(270000, true);
#ifdef DO_OVERCLOCK
#ifdef INCLUDE_ZEPTOCORE
  set_sys_clock_khz(225000, true);
#else
  set_sys_clock_khz(200000, true);
#endif
#else
  set_sys_clock_khz(125000, true);
#endif
  ZD_CALL(zd_init(clock_get_hz(clk_sys), SAMPLES_PER_BUFFER));
  sleep_ms(75);

  // DCDC PSM control
  // 0: PFM mode (best efficiency)
  // 1: PWM mode (improved ripple)
  gpio_init(PIN_DCDC_PSM_CTRL);
  gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
  gpio_put(PIN_DCDC_PSM_CTRL, 1);  // PWM mode for less Audio noise

#ifdef INCLUDE_ZEPTOCORE
  i2c_init(i2c_default, 50 * 1000);
  gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA_PIN);
  gpio_pull_up(I2C_SCL_PIN);
  sleep_ms(5);
  // detect if 0x61 exists
  uint8_t rxdata;
  int ret = i2c_read_timeout_us(i2c_default, 0x61, &rxdata, 1, false, 2000);
  if (ret < 0) {
    // normal
    is_arcade_box = false;
  } else {
    // arcade box
    is_arcade_box = true;
    led_text_time = 50;
    mcp23017_init(i2c_default, MCP23017_ADDR1);
    mcp23017_init(i2c_default, MCP23017_ADDR2);
    // addr1
    // set all as inputs
    mcp23017_set_dir_gpioa(i2c_default, MCP23017_ADDR1, 0b11111111);
    sleep_ms(1);
    mcp23017_set_dir_gpiob(i2c_default, MCP23017_ADDR1, 0b11111111);
    sleep_ms(1);
    mcp23017_set_pullup_gpioa(i2c_default, MCP23017_ADDR1, 0b11111111);
    sleep_ms(1);
    mcp23017_set_pullup_gpioa(i2c_default, MCP23017_ADDR1, 0b11111111);
    sleep_ms(1);
    mcp23017_set_pullup_gpiob(i2c_default, MCP23017_ADDR1, 0b11111111);
    sleep_ms(1);
    // addr2
    // set B as outputs and A as inputs
    mcp23017_set_dir_gpioa(i2c_default, MCP23017_ADDR2, 0b11111111);
    sleep_ms(1);
    mcp23017_set_dir_gpiob(i2c_default, MCP23017_ADDR2, 0b00000000);
    sleep_ms(1);
    mcp23017_set_pullup_gpioa(i2c_default, MCP23017_ADDR2, 0b11111111);
    sleep_ms(1);
  }
#endif

#ifdef LED_TOP_GPIO
  gpio_init(LED_TOP_GPIO);
  gpio_set_dir(LED_TOP_GPIO, GPIO_OUT);
#endif

#ifdef INCLUDE_ZEPTOCORE
  mode_buttons16 = MODE_SAMPLE_SLOT;
#endif
  if (!do_calibration_mode) {
    ap = init_audio();
  }

  // Implicitly called by disk_initialize,
  // but called here to set up the GPIOs
  // before enabling the card detect interrupt:
  sd_init_driver();

#ifdef INCLUDE_ZEPTOCORE
  // initialize adcs
  adc_init();
  adc_gpio_init(26);
  adc_gpio_init(27);
  adc_gpio_init(28);
#endif

  // init timers
  // Negative delay so means we will call repeating_timer_callback, and call
  // it again 500ms later regardless of how long the callback took to execute
  // add_repeating_timer_ms(-1000, repeating_timer_callback, NULL, &timer);
  // cancel_repeating_timer(&timer);
  // Start the playback timer after the persistent state has been allocated.
  // initialize random library
  random_initialize();

  // initialize message sync

  // initialize debouncers
  for (uint8_t i = 0; i < DEBOUNCE_UINT8_NUM; i++) {
    debouncer_uint8[i] = DebounceUint8_malloc();
  }

#ifdef INCLUDE_ZEPTOCORE
  debouncer_digits = DebounceDigits_malloc();
  leds = LEDS_create();
  ledtext = LEDText_create();
#endif

  // LEDText_display(ledtext, "HELLO");
  // show X in case the files aren't loaded
  // LEDS_show_blinking_z(leds, 2);

  sel_sample_next = 0;
  sel_variation_next = 0;
  sel_bank_cur = 0;
  sel_sample_cur = 0;
  sel_variation = 0;

  // printf("startup!\n");
  sdcard_startup();
#ifdef INCLUDE_ZEPTOCORE
  update_repeating_timer_to_bpm(project_get_bpm());
#else
  update_repeating_timer_to_bpm(sf->bpm_tempo);
#endif

  // TODO
  // load chain from SD card
  //   Chain_load(chain, &sync_using_sdcard);

#ifdef INCLUDE_RGBLED
  ws2812 = WS2812_new(23, pio0, 2);
  sleep_ms(1);
  WS2812_fill(ws2812, 0, 0, 0, 0);
  sleep_ms(1);
  WS2812_show(ws2812);
  // for (uint8_t i = 0; i < 255; i++) {
  //   WS2812_fill(ws2812, i, 0, 0);
  //   WS2812_show(ws2812);
  //   sleep_ms(4);
  // }
  // for (uint8_t i = 0; i < 255; i++) {
  //   WS2812_fill(ws2812, 0, i, 0);
  //   WS2812_show(ws2812);
  //   sleep_ms(4);
  // }
  // for (uint8_t i = 0; i < 255; i++) {
  //   WS2812_fill(ws2812, 0, 0, i);
  //   WS2812_show(ws2812);
  //   sleep_ms(4);
  // }
  // WS2812_fill(ws2812, 20, 20, 0);
  // WS2812_show(ws2812);
#endif

  fil_current_change = true;

#ifdef INCLUDE_ZEPTOCORE

  // EnvelopeLinearInteger_reset(
  //     envelope_filter, BLOCKS_PER_SECOND,
  //     EnvelopeLinearInteger_update(envelope_filter, NULL), 5, 1.618);
  // sf->fx_active[FX_FUZZ] = true;
  // sf->fx_active[FX_SATURATE] = true;
  // sf->fx_active[FX_SHAPER] = true;

#endif

  // blocking
  input_handling();
}
