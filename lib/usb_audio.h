#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#include <stdint.h>

// 0 is silent and 4095 is full scale. A + Z writes this.
extern volatile uint16_t usb_audio_level;

void usb_audio_poll(void);
void usb_audio_mix(int32_t *samples, uint32_t frames, int32_t output_gain);

#endif
