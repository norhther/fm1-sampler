# fm1-sampler

A **sampler + audio looper** (8 tracks) for Android with the M-VAVE FM-1 as its hardware front end. The phone does all the sound; the FM-1 is the
control surface (keys, buttons, 7 encoders), the second screen (240x240, drawn with SysEx commands) and the audio output (USB audio).
All DSP is original code in `engine/dsp` (no third-party audio engine); the only library is KissFFT (BSD) for the Paulstretch mode.

## Screenshots
FM-1 second screen (240x240, from the host simulator `app_sim`: sample, edit, envelope, LFO, arp, tape, mixer, loop length, key, mic waiting, rec armed, loop playing):

![FM-1 screens](shots/readme_fm1.png)

Phone app (sample with voice positions and the whole-loop lane, voices, LOOP page, LENGTH page, mic recording, library):

![Phone app](shots/readme_phone.png)

## Downloads
APK and the flash page of the matching firmware are attached to the [releases](../../releases) of this repository. Flash the firmware first (SEL = loop length page needs it), then install the APK.

## Quick start (OP-1 style)
The tape is the song, the sequencer only makes notes, the four knobs K1..K4 always control the page you are on.
1. **Pick a sound**: turn PRESETS to browse (it plays after you stop turning), SELECT picks the track (1..8).
2. **Play and record to tape**: REC, play keys, REC again. The first take sets the loop length and tempo. REC again to overdub, REC to stop.
3. **Make a pattern**: **hold SEQ and play keys** while it plays. Let go and it loops. Hold again to add more notes (overdub). K4 on the pattern page clears it.
4. **Put the pattern on tape**: with a pattern playing, **REC** records one pass of it onto the tape (the pattern is muted afterwards, the tape plays it). Move to the next track, repeat.
5. **Shape it**: FX for effects, MIX for volume, pan, key and scale.

## Modes
Four mode buttons, like the OP-1. Each has up to four pages. **Press the button again for the next page**, the four dots next to the page name show where you are, and a mode comes back to the page you left it on.

| Button | Mode | Pages |
|---|---|---|
| HOME | SOUND | SAMPLE, ENVELOPE, LFO, ARP |
| FX | FX | SENDS (reverb, delay), INSERT (chorus, flanger, phaser, compressor), TONE (drive, 3-band EQ), MOD (tremolo, auto-wah, ring mod, stereo width), LIVE (DJ filter, beat repeat, trance gate, crush), LIVE 2 (reverse, tape stop, freeze), TRACK (a set of the same effects on the selected track only) |
| SEQ | SEQ | PATTERN, TAPE (loop speed, exciter, lo-fi, wobble), TEMPO (tempo, metronome, take snap), TIMING (live quantize, pattern quantize, count-in) |
| EDIT | MIX | MIXER, KEY (scale, root, octave, mode), LOOP (mic sample, loop source, save), LENGTH |

ENV, LFO, ARP, GLO, SEL and SAVE are shortcuts to a page (ENVELOPE, LFO, ARP, KEY, LENGTH, LOOP). SAVE: tap saves the loop, hold clears all loops.
On the phone the same four modes are the buttons above the page tabs.

## FM-1 controls (all pages)
| Control | Does |
|---|---|
| SELECT (encoder 0) | track 1..8 |
| PRESETS (encoder 6) | browses the sample library; after 250 ms of rest the entry is loaded on the selected track and played (audition) |
| ALGORITHM (encoder 1) | MELODIC, ONE-SHOT, LOOPED, KIT/SLICE, GRANULAR, PAULSTRETCH (the Sampler's MODE; KIT slices on transients, equal parts if there are none) |
| K1..K4 (encoders 2..5) | the four parameters of the current page (below) |
| HOME | (SOUND mode, page 1) sample: START, END, SPEED (time stretch 0.25..4x), PITCH (+-24 st). Speed and pitch are offline stretches, applied 350 ms after the knob rests |
| FX | REVERB SIZE, REVERB D/W (return level), DELAY TIME, DELAY FEEDBACK (global sends; per-track send levels are on MIXER). FX mode, page 1 of 7 (then INSERT, TONE, MOD, LIVE, LIVE 2, TRACK) |
| INSERT | (FX mode, page 2) master inserts: CHORUS, FLANGER, PHASER, COMPRESSOR (4:1, threshold 0..-24 dB with half make-up) |
| TONE | (FX mode, page 3) DRIVE (soft clipping, the level is kept), EQ LOW (shelf at 120 Hz), EQ MID (peak at 1 kHz), EQ HIGH (shelf at 6 kHz), each +-12 dB, the centre is flat |
| MOD | (FX mode, page 4) TREMOLO (eighth notes on the beat clock), AUTO WAH (a band-pass that follows the level), RING MOD (carrier 20 Hz to 3 kHz), WIDTH (0 = mono, 50% = untouched, 100% = double) |
| LIVE | (FX mode, page 5) performance effects on the master, for turning while playing: DJ FILTER (centre = off, left low-pass, right high-pass), BEAT REPEAT (loops the last 1/4, 1/8, 1/16 or 1/32 beat until switched off), TRANCE GATE (1/8, 1/16, 1/32 on the beat clock), CRUSH (sample rate + bit depth). They are not recorded into the loop |
| LIVE 2 | (FX mode, page 6) REVERSE (the last 1/4, 1/8, 1/16 or 1/32 beat plays backwards, like BEAT REPEAT), TAPE STOP (turn up: the sound slows down and stops, back to 0 and it is live again), FREEZE (the last 150 ms are held as a pad). Not recorded into the loop |
| TRACK | (FX mode, page 7) effects on the selected track only: DJ FILTER, CHORUS, TRANCE GATE, CRUSH (after the cassette, before volume and sends) |
| ENV | ATTACK, DECAY, SUSTAIN, RELEASE of the selected track |
| LFO | RATE (0.1..20 Hz), DEPTH, TARGET (PITCH, FILTER, VOLUME, PAN), SHAPE (SINE, TRI, SQUARE, S&H) |
| ARP | (SOUND mode, page 4) RATE (OFF, 1..64 with triplets), PATTERN, OCTAVES, LENGTH (own arpeggiator on the tempo grid; it follows the tempo) |
| PATTERN | (SEQ mode, page 1) per track note pattern on the beat clock, locked to the loop. **Hold SEQ and play keys** to record (overdub: hold again to add), let go and it loops. K1 PATTERN (OFF, PLAY, REC), K2 BARS (1, 2, 4, 8; longer repeats the content, shorter drops the rest), K3 QUANTIZE (1/16 by default; applied when the pattern plays, the recorded timing is kept so it can be changed or switched off later), K4 CLEAR. **REC** while the pattern plays records one pass onto the tape (a new loop of the pattern's length when there is none, otherwise it waits for the loop start) and mutes the pattern. The phone shows a piano roll: tap a cell to add or remove a note, drag up and down to scroll. UNDO / REDO (16 steps) take back and restore the last edit: a recording pass, a tap on the grid, a clear, a change of the length. TIME -/+ and PITCH -/+ make the grid smaller or bigger (zoomed in, drag sideways to scroll, the view follows the playhead), GRID sets the cell size (1/4 ... 1/32T, the same as QUANTIZE). Notes go through the track's arpeggiator when it is on. 256 notes per track |
| TAPE | (SEQ mode, page 2) LOOP SPEED (0.25..2x tape-style varispeed of the loop, GLOBAL for all tracks), EXCITER (cassette saturation), LO-FI (bit crush + hiss + dark tone), WOBBLE |
| EDIT | mixer of the selected track: VOLUME, PAN, REVERB SEND, DELAY SEND |
| GLO | (MIX mode, page 2, shortcut) key and scale: SCALE (22: the 7 firmware tables, DORIAN, LYDIAN, LOCR, PENT+, PENT-, BLUES, WHOLE, DIM, PHRDOM, HMAJ, MELMIN, HUNG, HIRA, DHARM, OFF last), ROOT, OCTAVE, MODE (KEY, CHORD, SNAP: keys outside the scale play the nearest scale note). |
| TEMPO | (SEQ mode, page 3) TEMPO (30..300; locked while a loop exists, the loop defines it), METRONOME (click on the beat clock, accent on the bar), CLICK LEVEL, TAKE SNAP (OFF: the first take sets the tempo; BEAT / BAR: the tempo stays and the take is cut or padded to whole beats / bars, i.e. the loop is quantized) |
| TIMING | (SEQ mode, page 4) LIVE QUANTIZE (OFF, 1/4 ... 1/32T: a played key sounds on the nearest grid line of the beat clock, a key that is a little late plays at once; sequencer recording stores the snapped time), PATTERN QUANTIZE (the same setting as on the pattern page), COUNT-IN (OFF, 1 or 2 bars of clicks, with the metronome off too, before a free take or a sequencer recording from a stopped transport; keys played during the count-in do not start the take) |
| SAVE | page LOOP. Tap = save the loop of the selected track into the library, hold 0.7 s = clear ALL loops (every track), also the phone CLEAR button. Knobs: MIC SAMPLE, LOOP SOURCE, SAVE LOOP, MIC THRESHOLD (-46..-6 dB, level that starts a mic take; used by mic sampling and mic loop source) |
| PLAY | play / stop |
| REC | always the tape. While the selected track's pattern plays it records one pass of the pattern onto the tape instead (see PATTERN). Clean looper: arms the first take (it starts with the first key or sound and sets the loop length and tempo; REC again ends it). Loop exists: toggles overdub on the selected track |
| SEL (the button after FX; index 3, the protocol and the firmware call it SCL) | page LENGTH: K1 LENGTH (turn up = loop x2 with its content repeated, down = /2), K2 DUPLICATE, K3 HALVE, K4 bars/bpm (read-only). Needs the firmware built here (`firmware/FM1-Sampler-Workbench.html`): it forwards SCL as button 3 and has no scale screen; the stock/fm1-daw firmware keeps SCL local |
| OCT -/+ | handled by the firmware (octave) |
| keys | KIT algorithm: all 27 keys are pads (note 60 + key, 16 slices repeat over the keys), every key lit, tonality and octave ignored. Otherwise the firmware is kept on a plain chromatic keyboard (scale OFF, SysEx 06 always sends 7; notes 53+key) so keys outside the tonality still play; the app adds the octave, builds triads itself in CHORD mode and lights ONLY keys that belong to the scale (root of each octave blinks; scale OFF: only roots); a sounding key goes dark. |

## Beat clock, sequencer, master fx
- Effects: FX 2 / LIVE FX run on the master, TRACK FX runs one `MasterFx` per track (`active()` skips it at rest values, `feedOnly()` keeps the repeat memory warm).
- The beat clock is the transport position (it restarts with the loop) and runs free while stopped, so the metronome also ticks before a take; the take starts the clock at its first note. Sequencer notes, metronome, trance gate and beat repeat all read it (`engine/dsp/Sequencer.h`: 192 ticks per beat, `tickHits` lists the ticks of an audio block with sample offsets).
- `engine/dsp/MasterFx.h`: the FX 2 and LIVE FX chain (chorus, phaser, width, DJ filter, crusher, gate, repeat, compressor). All of it sits after the sends and before the soft clip; everything is bypassed at its rest value.
- Tests of the added features: `make $PWD/engine/build/test_features` (run from `engine/`), then `engine/build/test_features`.

## Sample sources
- **Bundled library** (`samples/`, packed into the APK assets by Gradle, copied to `files/samples` on the first start and again after every update): Bass, Drums (loaded as KIT), Keys, Leads, Pads. 47 sounds. 30 of them are synthesized by `tools/gen_instruments.py` (numpy / scipy, deterministic, no recordings): Keys: E-Piano, Wurly, Organ, Marimba, Music Box, Bell, Harpsichord, Clav, Vibraphone. Bass: 808 Sub, Saw Bass, Square Bass, Pluck Bass, Reese, Funk Bass. Pads: Strings, Choir, Warm Pad, Glass Pad, Brass Pad, Dark Pad. Leads: Square Lead, Saw Lead, Chip, Flute, Pluck Lead, Sync Lead. Drums: 808 Kit, Electro Kit, Lo-Fi Kit (16 hits each, one per pad). Melodic samples are tuned to C4 (basses to C2): they play at their own pitch on MIDI note 60. Run `python3 tools/gen_instruments.py` to rebuild them.
- **Microphone**: phone button "RECORD FROM MIC" or LOOP page K1. Waits for sound above the trigger level, trims and normalises, saves `sample_<ms>.wav` in the data dir, loads it on the track it was started on.
- **Disk**: phone button "LOAD FILE FROM DISK" -> system picker -> `AudioImport.java` converts to a 16-bit WAV (`sample_<name>.wav` in `files/`): WAV 8/16/24/32/float directly, anything else through MediaCodec (mp3, aac, ogg, flac). Max 90 s.
- **Saved loops** and the two above all live in the "User" group of the library (`sample_*.wav` in the data dir).
Browsing replaces the sample of the selected track; a new sampler keeps the track's settings (`applyAll` after `sampleVersion` bumps).
At start: Tape Key on track 1, Acoustic bass on 2, Shiny on 3, TrapKit (KIT) on 4.

## Looper details
- Loop source (LOOP page K2, or the label in the phone top bar): SOUND = what the track plays (keys, arp, sample, filter, before the cassette) or MIC. Default SOUND.
- One clip per track; all tracks share the loop length of the first take. All loops are read (and recorded) through ONE tape pointer (`Session::tapePos`), so LOOP SPEED moves them together. At 1.0x the pointer is locked to the transport.
- Recording into the loop works at any speed like a tape head: below 1x the input is spread over the two neighbouring cells weighted by the speed, above 1x the skipped cells are filled by interpolating the input.
- A free take records audio only (no notes), so nothing in the loop runs at a speed of its own.
- Exciter / lo-fi / wobble are global: a per-track cassette (saturation, tone, wow and flutter, hiss, mix) followed by a bit crusher.
- Reverb and delay run 100 % wet as send returns; D/W is the return gain (`Session::setRetLevel`).
- The whole-loop overview in the phone UI (`Session::loopPeaks`) is refreshed by the audio thread, 8 of 400 bins per block.

## Engine
- `engine/dsp/Sampler.*`: the instrument. Up to 8 voices; MELODIC, ONE-SHOT, LOOPED (crossfaded loop), KIT (16 slices cut at the hits by `Slicer.h`, pad = note - 60), GRANULAR (Hann grains at random positions in the window), PAUL (`Paul.*`: windowed spectra with random phases, overlap-add, at most 4 voices; the spectral hops are generated ahead, one half hop per block for the voice that needs it most (`Sampler::pumpPaul`), so the notes of a chord do not put their FFTs in the same audio block; a new voice waits for its first hop, a voice pushed out by a new note fades out in 8 ms in every algorithm). `Adsr.h`, `Filter.h` (state variable low-pass, used by the LFO filter target).
- `engine/dsp/Stretch.*`: offline time stretch (WSOLA) and pitch shift (stretch + resample) of the sample window; built on the worker thread 350 ms after the SPEED / PITCH / START / END knob rests, then swapped in.
- `engine/dsp/Reverb.h` (8 combs + 4 allpasses per side), `TapeDelay.h`, `TapeInsert.h`, `Arp.h` (UP, DOWN, UPDN, CONV, RAND over 1..4 octaves, sample accurate gate), `Wav.*`.
- `engine/session/Session.*`: tracks, mixer, LFO (once per block, ~170 Hz: pitch, filter, volume or pan), looper, mic recorder, library listing, command queue to the audio thread, worker thread. `status()` is lock-free.
- `engine/app/SamplerApp.*`: all behaviour: knob pages (`knobs(Page)` is the single source for FM-1 and phone), per-track state, library, looper, FM-1 screen drawing and LEDs, SysEx pacing and loss recovery.
- `engine/ui/SamplerUI.*`: the phone UI as draw ops (left tracks, centre sample/waveform with draggable START/END, voice markers, whole-loop lane, right page tabs + 4 sliders, library sheet). `ui/UiOp.h`, `Theme.h` (paper/ink/amber, square corners).
- `android/`: `Backend` (FM-1 USB MIDI + USB audio routing, Oboe, 30 Hz tick), `SamplerActivity` (file picker, mic permission), `AudioImport`, `SamplerView` (replays ops), `native-lib.cpp`. Package `com.ferluht.fm1sampler`, lib `fm1sampler`.
- `firmware/`: the FM-1 firmware (M-VAVE's open firmware with the control-surface protocol of `PROTOCOL.md`). `firmware/FM1-Sampler-Workbench.html` is the flash page; `tools/build_fw.py firmware ~/fm1/wine/drive_c/JL/pi32` rebuilds it (JieLi toolchain under wine, ~30 s). Changes in `app/ctrl.c`: SEL (BTN_SCL) forwarded to the app (no on-device scale screen), splash "SAMPLER".

## Build and test
- Host: `make -C engine -j4`, then `make $PWD/engine/build/<t>` with t = `test_app` (checks: every algorithm, LFO targets, stretch and pitch, tape, arp, loop, speed, overdub, loop length, voices, KIT keys, save/clear, mic, disk import, audition, DSP units), `app_sim` (virtual FM-1, writes `app_NN_*.ppm` screenshots of its screen), `phone_render` + `python3 engine/tools/render_ui.py ui_ops.json outdir 1.5` (phone screenshots), `probe` (what the default project loads). Paths in the tools assume `~/fm1-sampler/samples`. The tools sleep a little per audio block so the worker thread (sample loading) keeps up with the simulated time.
- Android: `cd android && ANDROID_HOME=~/android-sdk ~/gradle/gradle-8.9/bin/gradle assembleDebug -q` (~4 min first, never edit engine sources while it compiles: clang then reports "source file is not valid UTF-8"). Output `app/build/outputs/apk/debug/app-debug.apk`.

## Status
Host-tested only (test_app all green, screenshots of both screens checked). **Not run on a phone or an FM-1.** Open questions: MediaCodec import on a real phone, offline stretch time on the phone CPU, Paulstretch / granular CPU on the phone, arp is free running (not locked to the loop), loop clips are not saved with a project (no project save/load).
