#ifndef USB_RECORD_H
#define USB_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#define RESERVE_DATA_OFF 88576u
/* 20 s of 44.1 kHz 16-bit mono. */
#define RESERVE_AUDIO_BYTES (20u * 44100u * 2u)
#define RESERVE_SLOT_BYTES (RESERVE_DATA_OFF + RESERVE_AUDIO_BYTES)
#define RESERVE_CLMT 64
/* Shared with the sample queues. Recording starts only after those queues stop. */
/* One quarter of the shared record buffer: 4 * 6381 * 2 fits in REC_CAP_FRAMES * 2. */
#define SAMPLE_SLOT_PREF_FRAMES 6381u
extern int16_t sample_slot_pref_mem[];

extern bool sample_slot_reserve_ok;
void sample_slot_reserve_check(void);
void sample_slot_reserve_maps_load(void);
bool sample_slot_reserve_load(uint8_t slot, uint32_t *clmt);
bool sample_slot_format_cached(uint8_t slot);
bool sample_slot_format(uint8_t slot, uint8_t *channels);
void sample_slot_format_set(uint8_t slot, uint8_t channels);
bool sample_slot_direct_ready(void);
bool sample_slot_direct_read(uint8_t slot, uint32_t offset, uint8_t *dst,
                             uint32_t len, uint32_t *got);
void usb_record_arm(uint8_t slot);
void usb_record_poll(bool d_held);
bool usb_record_full_flash(void);
void usb_record_observe(int16_t left, int16_t right);
bool usb_record_blocks_card(void);
void usb_record_card_free(void);
int8_t usb_record_pad(void);
bool usb_record_started(void);
void sample_slot_voices_for_card(void);
bool usb_record_claim_card(void);
bool usb_record_claim_ready(void);
void usb_record_release_card(void);
bool sample_slot_transfer(uint8_t src, uint8_t dest);
bool sample_slot_resample_open(uint8_t slot);
bool sample_slot_resample_write(const uint8_t *data, uint32_t bytes);
bool sample_slot_resample_add(const uint8_t *data, uint32_t bytes);
bool sample_slot_resample_rewind(void);
void sample_slot_resample_commit(bool keep);

#endif
