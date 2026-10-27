#include "tusb.h"
#include "usb_audio.h"
#include "usb_record.h"

volatile uint16_t usb_audio_level = 4095;

#define USB_AUDIO_FRAMES 1024
#define USB_AUDIO_MASK (USB_AUDIO_FRAMES - 1)

static int16_t usb_ring[USB_AUDIO_FRAMES][2];
static volatile uint32_t usb_head;
static volatile uint32_t usb_tail;
static int16_t usb_hold_l;
static int16_t usb_hold_r;

static int32_t usb_qmul(int32_t a, int32_t b) {
  return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
}

static void usb_audio_push(uint8_t const *packet, uint16_t len) {
  len &= (uint16_t)~3u;
  for (uint16_t i = 0; i < len; i += 4) {
    uint32_t next = (usb_head + 1) & USB_AUDIO_MASK;
    if (next == usb_tail) continue;
    usb_ring[usb_head][0] = (int16_t)(packet[i] | (packet[i + 1] << 8));
    usb_ring[usb_head][1] = (int16_t)(packet[i + 2] | (packet[i + 3] << 8));
    __dmb();
    usb_head = next;
  }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes);
void tud_rp2040_iso_out_clear_avail(uint8_t ep_addr);

// Re-arm this buffer. The interrupt can hand over the controller's own
// packet memory, which must not become the next transfer buffer.
static uint8_t usb_arm_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX];

void tud_rp2040_iso_out_complete(uint8_t ep_addr, uint8_t *buffer, uint16_t len) {
  if (buffer && len >= 4) usb_audio_push(buffer, len);
  tud_rp2040_iso_out_clear_avail(ep_addr);
  dcd_edpt_xfer(0, ep_addr, usb_arm_buf, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX);
}

void dcd_sof_enable(uint8_t rhport, bool en);
void audio_i2s_nudge_clock(int direction);

static volatile uint32_t usb_sof_count;
static uint8_t usb_sof_enabled;

void tud_rp2040_sof_tick(void) { usb_sof_count++; }

void usb_audio_poll(void) {
}

void usb_audio_mix(int32_t *samples, uint32_t frames, int32_t output_gain) {
  int32_t output = output_gain;
  int32_t input = ((int32_t)usb_audio_level) << 4;
  int32_t gain = usb_qmul(output, input);
  if (gain <= 0) {
    usb_tail = usb_head;
    usb_hold_l = 0;
    usb_hold_r = 0;
    return;
  }

  if (!usb_sof_enabled) {
    dcd_sof_enable(0, true);
    usb_sof_enabled = 1;
  }
  static uint32_t played;
  static uint32_t last_sof;
  played += frames;
  uint32_t sof = usb_sof_count;
  uint32_t elapsed = sof - last_sof;
  if (elapsed >= 1000) {
    int32_t expected = (int32_t)((elapsed * 44100u) / 1000u);
    int32_t err = (int32_t)played - expected;
    if (err > 441) audio_i2s_nudge_clock(1);
    else if (err < -441) audio_i2s_nudge_clock(-1);
    played = 0;
    last_sof = sof;
  }

  uint32_t tail = usb_tail;

  for (uint32_t i = 0; i < frames; i++) {
    int16_t left_s;
    int16_t right_s;
    if (tail == usb_head) {
      left_s = usb_hold_l;
      right_s = usb_hold_r;
    } else {
      left_s = usb_ring[tail][0];
      right_s = usb_ring[tail][1];
      usb_hold_l = left_s;
      usb_hold_r = right_s;
      tail = (tail + 1) & USB_AUDIO_MASK;
    }
    __dmb();
    usb_tail = tail;
    int32_t heard_l = ((int32_t)left_s * (int32_t)usb_audio_level) / 4095;
    int32_t heard_r = ((int32_t)right_s * (int32_t)usb_audio_level) / 4095;
    if (heard_l > 32767) heard_l = 32767;
    if (heard_l < -32768) heard_l = -32768;
    if (heard_r > 32767) heard_r = 32767;
    if (heard_r < -32768) heard_r = -32768;
    usb_record_observe((int16_t)heard_l, (int16_t)heard_r);
    int32_t left = usb_qmul((int32_t)left_s << 16, gain);
    int32_t right = usb_qmul((int32_t)right_s << 16, gain);
    int64_t mixed_l = (int64_t)samples[i * 2] + left;
    int64_t mixed_r = (int64_t)samples[i * 2 + 1] + right;
    if (mixed_l > 2147483647) mixed_l = 2147483647;
    if (mixed_l < -2147483648LL) mixed_l = -2147483648LL;
    if (mixed_r > 2147483647) mixed_r = 2147483647;
    if (mixed_r < -2147483648LL) mixed_r = -2147483648LL;
    samples[i * 2] = (int32_t)mixed_l;
    samples[i * 2 + 1] = (int32_t)mixed_r;
  }
}

static int16_t mute[3];
static int16_t volume[3] = {0, 0, 0};

static bool usb_audio_clock_get(uint8_t rhport,
                                audio_control_request_t const *request) {
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
      request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_4_t curf = {.bCur = (int32_t)tu_htole32(44100)};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
  }
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
      request->bRequest == AUDIO_CS_REQ_RANGE) {
    audio_control_range_4_n_t(1) rangef = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = {.bMin = 44100, .bMax = 44100, .bRes = 0}};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &rangef,
        sizeof(rangef));
  }
  if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
      request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t cur_valid = {.bCur = 1};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &cur_valid,
        sizeof(cur_valid));
  }
  return false;
}

static bool usb_audio_clock_set(audio_control_request_t const *request,
                                uint8_t const *buf) {
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ &&
      request->bRequest == AUDIO_CS_REQ_CUR &&
      request->wLength == sizeof(audio_control_cur_4_t)) {
    uint32_t rate = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
    return rate == 44100;
  }
  return false;
}

static bool usb_audio_feature_get(uint8_t rhport,
                                  audio_control_request_t const *request) {
  if (request->bChannelNumber >= 3) return false;
  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE &&
      request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t mute_cur = {
        .bCur = (uint8_t)mute[request->bChannelNumber]};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &mute_cur,
        sizeof(mute_cur));
  }
  if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME &&
      request->bRequest == AUDIO_CS_REQ_RANGE) {
    audio_control_range_2_n_t(1) range_vol = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = {.bMin = tu_htole16(0), .bMax = tu_htole16(0),
                        .bRes = tu_htole16(1)}};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &range_vol,
        sizeof(range_vol));
  }
  if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME &&
      request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_2_t cur_vol = {
        .bCur = tu_htole16(volume[request->bChannelNumber])};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &cur_vol,
        sizeof(cur_vol));
  }
  return false;
}

static bool usb_audio_feature_set(audio_control_request_t const *request,
                                  uint8_t const *buf) {
  if (request->bRequest != AUDIO_CS_REQ_CUR || request->bChannelNumber >= 3)
    return false;
  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE &&
      request->wLength == sizeof(audio_control_cur_1_t)) {
    mute[request->bChannelNumber] =
        ((audio_control_cur_1_t const *)buf)->bCur;
    return true;
  }
  if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME &&
      request->wLength == sizeof(audio_control_cur_2_t)) {
    volume[request->bChannelNumber] =
        ((audio_control_cur_2_t const *)buf)->bCur;
    return true;
  }
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request) {
  audio_control_request_t const *request =
      (audio_control_request_t const *)p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return usb_audio_clock_get(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
    return usb_audio_feature_get(rhport, request);
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *p_request,
                                 uint8_t *buf) {
  (void)rhport;
  audio_control_request_t const *request =
      (audio_control_request_t const *)p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return usb_audio_clock_set(request, buf);
  if (request->bEntityID == UAC2_ENTITY_FEATURE_UNIT)
    return usb_audio_feature_set(request, buf);
  return false;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received,
                                    uint8_t func_id, uint8_t ep_out,
                                    uint8_t cur_alt_setting) {
  (void)rhport;
  (void)n_bytes_received;
  (void)func_id;
  (void)ep_out;
  (void)cur_alt_setting;
  // The USB interrupt already copied this packet and armed the next one.
  return false;
}

bool tud_audio_set_itf_cb(uint8_t rhport,
                          tusb_control_request_t const *p_request) {
  (void)rhport;
  (void)p_request;
  return true;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport,
                                   tusb_control_request_t const *p_request) {
  (void)rhport;
  (void)p_request;
  usb_head = 0;
  usb_tail = 0;
  return true;
}
