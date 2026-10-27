// Copyright 2026 Zack Scholl, GPLv3.0
#ifndef LIB_MIDI_NOTE_KEY_H
#define LIB_MIDI_NOTE_KEY_H

// Included after the playback globals and audio callback state. Other devices
// retain their existing defaults; -DMIDI_NOTE_KEY=0 disables slice notes.
#ifndef MIDI_NOTE_KEY
#define MIDI_NOTE_KEY 0
#endif

void midi_note_off(int note) {
  // Slice notes are triggers, not gates. Release never changes transport/mute.
  (void)note;
}

void midi_note_on(int note, int velocity) {
  (void)note;
  (void)velocity;
}

#endif
