#ifndef PROJECT_H
#define PROJECT_H

#include <stdbool.h>
#include <stdint.h>

extern volatile bool project_dirty;
extern uint8_t project_bank;
extern bool project_mode;
extern bool project_confirm;

extern volatile bool sample_slot_chain_hold_save;

static inline void project_mark(void) {
  project_dirty = true;
  sample_slot_chain_hold_save = false;
}

void project_boot(void);
void project_load(void);
void project_poll(void);
bool project_select(uint8_t bank);
bool project_clear(void);
void project_select_prepare(void);
bool project_bank_used(uint8_t bank);

#endif
