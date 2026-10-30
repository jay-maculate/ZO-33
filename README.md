# ZO-33

## What is ZO-33?
ZO-33 is a custom firmware for the Zeptocore open source sample player which adds capabilities like sampling and pattern sequencing to emulate the workflow and experience of using the PO-33 Pocket Operator. 

Key features:
- Record up to 16 samples over USB C, which can be sliced or played chromatically
- Samples are 16 bit 44.1 kHz mono WAV files, with max length of 20 seconds for each slot
- Create and chain patterns using the 16-step sequencer
- Save up to 16 projects
- Maximum of 4 voices of polyphony
- Resample feature for quickly turning audio of patterns into a single sample

## How to Get Started
Download the UF2 firmware file here: https://github.com/jay-maculate/ZO-33/releases 

Format your SD card, then use the Prepare Card tool here: https://jay-maculate.github.io/ZO-33/tools/prepare-card/

Boot the Zeptocore into firmware upload mode by holding the bottom of the two buttons on the Raspberry Pi, then plugging in the unit. While still holding that button, press the top button on the Raspberry Pi, then release both buttons.

There should be a window with the USB drive called RPI RP2 on your computer. Drag and drop the Uf2 firmware file into that window.

The Zeptocore should boot into the new firmware and initialize the card for use. After the LEDs stop flashing, the device is ready to use. The card only needs to be prepared and initialized once. 

## Button and Knob Designations
The knobs and the top row of buttons are represented by letters (knobs: X, Y, and Z // buttons: A, B, C, and D). The rest of the buttons are represented by numbers (1-16).

The top buttons generally perform these functions:

- A is the Play button. It is used to start and stop playback of the pattern, and used in shortcuts related to the project.
- B is the Sample Slot button. It is used to select the active sample, and used in shortcuts related to samples.
- C is the Pattern button. It is used to select the active pattern, and used in shortcuts related to patterns.
- D is the Write/Edit button. It is used to enter step edit mode where notes can be drawn on the grid, and used in shortcuts that write (i.e. copy) or delete data.

The X, Y, and Z knobs control different parameters depending on what button is held while turning the knob. A full list of mappings appears in the All Shortcuts section.

## How to Adjust the Main Output Volume, USB Input Volume, and Tempo
Main Output volume: Hold A and turn the X knob.

USB Input volume: Hold A and turn the Y knob.

Tempo: Hold A and turn the Z knob. There is no display to show the tempo - turning the knob fully left will be 60 BPM and fully right will be 180 BPM.

## How to Record a Sample
Hold D, then press one of the 16 pads to arm the recording. The pad will blink to show the recording is armed, and the recording will become active when the audio exceeds -24 dB. The recording will continue until the D button is released (the numbered pad does not need to be held after the recording is armed) or when the recording reaches the limit, 20 seconds.

Pads with samples already saved to them are dimly lit. Pads that do not have samples saved to them will not be lit. A recording will overwrite an existing sample slot if one is selected.

Note: there can only be 16 samples in a project, one sample per slot (button). Samples can only be recorded over USB when the Zeptocore is connected to a host audio device (phone/computer, etc.).

## How to Select a Sample Slot
Hold B, then select one of the 16 pads. The active selection will blink. Slots with samples saved to them will be dimly lit, and slots with no samples will not be lit.

After a sample is selected, it can be played by pressing any of the 16 keys. The samples will always start in chromatic mode.

## How to Adjust the Start and End Point of a Sample
Turn the Y knob without any other button held to adjust the start point of the sample, and turn the Z knob without any other button held to adjust the end point of the sample.

Hold D and turn those knobs to make adjustments in smaller increments. When holding D, the knob pointed straight up will keep the point where it currently is, then the left zone will move the point back and the right zone moves it forward within a smaller section of the sample than the full length of the recording.

For adjusting the start and end point of slices, see the mode switch section below.

## How to Switch a Sample Slot’s Mode (Chromatic/Slice)
Hold B, then turn the X knob. Turning this knob in any zone to the left will cause the LEDs to blink in a C and will  put the sample slot in Chromatic mode, in which the 16 pads will play different pitches of the sample. By default the sample will be in Chromatic mode. 

Turning this knob in any zone to the right will cause the LEDs to blink in an S and will put the sample slot in Slice mode, in which the 16 pads can trigger the audio of the sample from different points. When switching to Slice mode, the 16 pads will be assigned to points in the sample as if it were divided into 16 even parts. 

These 16 even slices will fall within the start and end point set while the sample is in chromatic mode. Try adjusting the start/end point in that mode to get slices in different places in the sample.

The start point of the last selected slice can be adjusted by turning the Y knob without any other button held, and the end point can be adjusted by turning the Z knob without any other button held. 

Hold D and turn those knobs to make adjustments to the last selected slice in smaller increments. When holding D, the knob pointed straight up is no adjustment, then the left zone will move the point back and the right zone moves it forward within a smaller section of the sample than the full length of the recording.

## How to Adjust the Sound of a Sample
Filter cutoff: Hold B and turn the Y knob. Turning left will enable the low pass filter, and turning right will enable the high pass filter.

Resonance: Hold C and turn the Z knob.

Attack: Hold C and turn the X knob.

Decay: Hold C and turn the Y knob.

Release: Hold C and turn the Z knob.

Level: Hold D and turn the X knob.

Pitch: Turn the X knob with no other buttons held.

## How to Use the Step Sequencer
Press D to toggle the step sequencer mode. While this mode is active, the LED for the D button will be lit. Press one of the 16 pads to add a note for the currently selected sample on that step. The note will play whatever pitch (if in chromatic mode) or slice (if in slice mode) you last selected. Select a pad again to remove the note from that step. Press D again to exit step sequencer mode.

The steps that have notes drawn to them will be lit, and steps with no notes are not lit. Steps that are blinking are steps that have the last pitch/slice you selected on them.

Each sample slot has its own sequence, so to adjust the sequence of a different sample slot, Hold B and select that slot’s pad to see the step sequencer for that sample.

## How to Add Notes to the Pattern in Real Time
You can input notes into the sequence as it plays live by pressing A to play the pattern, then holding D and pressing the pads of notes as the pattern plays. 

## How to Select a Pattern or Pattern Chain
Hold C, then select one of the 16 pads. The active selection will blink. Pattern slots with note data saved to them will be dimly lit, and slots with no notes will not be lit.

To chain patterns so they play in a specific order after pressing the play button (A), hold C and then select the pattern slots in the order you want them to be played. There is a limit of 64 patterns in a chain. Release C to finalize the chain.

Press A to play the pattern chain (or single pattern if only one is selected). Press A again to stop playback.

## How to Copy and Delete Samples
To copy a sample to another slot, select the sample you want to copy by holding B and pressing one of the 16 pads. Then hold B and press D to toggle the sample copy mode. With this mode toggled, you can release the B and D buttons. Then select the slot you want to paste the sample to and the sample will be pasted there. 

To delete a sample, select the sample you want to delete by holding B and pressing one of the 16 pads. Then hold B and press D to toggle the sample copy mode. With this mode toggled, you can release the B and D buttons. Then select the pad for the sample you have selected. The LEDs will blink in an X. To confirm the delete operation, press the pad of the sample you have selected again, which will be lit solid. To exit and cancel the delete operation, press the D button.

## How to Copy and Delete Patterns
To copy a pattern to another slot, select the pattern you want to copy by holding C and pressing one of the 16 pads. Then hold C and press D to toggle the pattern copy mode. With this mode toggled, you can release the C and D buttons. Then select the slot you want to paste the pattern to and the pattern will be pasted there. 

To delete a sample, select the pattern you want to delete by holding C and pressing one of the 16 pads. Then hold C and press D to toggle the pattern copy mode. With this mode toggled, you can release the C and D buttons. Then select the pad for the pattern you have selected. The LEDs will blink in an X. To confirm the delete operation, press the pad of the pattern you have selected again, which will be lit solid. To exit and cancel the delete operation, press the D button.

## How to Resample a Pattern
Resampling is a way to capture the audio of patterns so that the sounds of the pattern become a single WAV file and therefore only use one voice of polyphony. To resample, first select the pattern or pattern chain that you want to resample. 

Then hold D and press C to toggle the resample mode. The LED for D will be lit and the LED for C will be blinking (you can release D and C after this). Now the LEDs will be lit to show you which slots have samples loaded to them. Select an empty slot to save the resample recording there. Press D to exit resample mode.

Note: the recording can only be 20 seconds long, so a long pattern chain will only record the first 20 seconds. And a slot that has a sample loaded to it cannot be selected, only a blank slot can be selected as a destination for the resample recording.

## How to Select a Project
Hold A and press D to toggle the project select mode. The pads that are lit have projects on them, and the pads that are not lit are empty. The currently selected project will blink.

Select any pad to load that project. Press D to return to the current project.

## How to Delete a Project
Hold A and press D to toggle the project select mode. Press the pad of the current project (the blinking LED) to delete it. The LEDs will blink in the shape of an X and the current project’s LED will remain solid. Press the pad of the current project to confirm the delete operation, or press D to cancel the delete and return to the current project.

## All Shortcuts List
Global knob controls:

- Hold A then turn X: Adjust the main output volume
- Hold A then turn Y: Adjust the USB input volume
- Hold A then turn Z: Adjust the tempo, with a range of 60-180 BPM


Sample edit knob controls: 

- Turn X (no additional button held): Adjust sample pitch
- Turn Y (no additional button held): Adjust sample (or slice) start point
- Turn Z (no additional button held): Adjust sample (or slice) end point
- Hold D then turn X: Adjust sample level (velocity)
- Hold D then turn Y: Adjust sample (or slice) start point (fine edit - the knob pointed straight up is no adjustment, then turning left move the start point back and right moves it forward within a smaller zone than the full length of the recording)
- Hold D then turn Z: Adjust sample (or slice) end point (fine edit - the knob pointed straight up is no adjustment, then turning left move the start point back and right moves it forward within a smaller zone than the full length of the recording)


Sample Playback knob controls:
- Hold B then turn X: Change sample slot to chromatic mode (left zone of knob) or slice mode (right zone of knob)
- Hold B then turn Y: Adjust filter cutoff frequency (left is low pass, right is high pass)
- Hold B then turn Z: Adjust resonance of filter
- Hold C then turn X: Adjust envelope attack of sample (or all slices)
- Hold C then turn Y: Adjust envelope decay of sample (or all slices)
- Hold C then turn Z: Adjust envelope release of sample (or all slices)
  

Step edit knob controls:
- Hold a step, then turn X: Adjust Microtiming of that step from +0% to +50%
- Hold a step, then turn Y: Adjust velocity (volume) of that step
  

Copy/Delete shortcuts

- Hold B then press D: Toggle sample copy mode. Press a pad to paste the sample there, press D to exit, or press the current sample slot twice to delete it
- Hold C then press D: Toggle pattern copy mode. Press a pad to paste the pattern there, press D to exit, or press the current pattern slot twice to delete it


Project shortcuts:
- Hold A then press D to toggle project select mode. Select a pad to laid that project, press D to exit and return to the current project, or press the current project slot twice to delete it


Resample shortcut:
- Hold D and press C to toggle resample mode. Select a pad to capture the audio of the pattern into a single WAV file to that slot, or press D to exit.

