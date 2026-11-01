# fm1-sampler

A **sampler + audio looper** (8 tracks, up to 16 with ADD TRACK) for Android with the M-VAVE FM-1 as its hardware front end. The phone does all the sound; the FM-1 is the
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
| SEQ | SEQ | PATTERN, TAPE (loop speed, exciter, lo-fi, wobble), TEMPO (tempo, metronome, take snap), TIMING (live quantize, pattern quantize, count-in, swing), TOMBOLA |
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
| SHAPE | (SOUND mode, page 5) for a **built-in synth**: TONE, BODY, MOTION, GRIT, four macros that reshape the patch (brightness, weight and length, movement, drive). For a **sample**: ROOT KEY (the key that plays it at its own pitch, C4 for the library), AUTO TUNE (detects the pitch of the sample and sets the root key to it), SLICES (4..32 pads for KIT mode) and SLICE SENS (how soft a hit may be); the waveform shows the pad starts in KIT mode |
| OP-1 FX | (FX mode, page 8) SPRING (reverb with the metallic boing) and its DAMP, PHONE (telephone band, a few bits, hiss), PUNCH (transients and saturation) |
| FX 5 | (FX mode, page 9; the same four on the selected track: MIX tab, TRACK FX 6) COMB (a feedback comb filter that rings at the note of COMB NOTE), OCTAVE (centre = off, left mixes in the octave below, right the octave above: a delay-line shifter, so a pure tone gets a little grain), FREQ SHIFT (centre = off, moves every partial by the same number of Hz, up to +-800: bells and metal) |
| FX 6 | (FX mode, page 10; the same on the selected track: MIX tab, TRACK FX 7) SCRATCH, a turntable simulator: the last 0.8 s of sound sit on a record that is pushed and pulled in time with the beat. STYLE: BABY (smooth push and pull), SCRIBBLE (fast small shake), RUN BACK (quick rewind, slow catch-up), TRANSFORM (the fader chops the sound in eighths). CYCLE: 2 beats down to 1/8 beat. NEEDLE: the surface noise when the record moves fast. SCRATCH is how hard the hand pushes; the record always catches up with the live sound at the end of each cycle |
| TOMBOLA | (SEQ mode, page 5) like the OP-1 tombola: balls bounce in a turning polygon and every hit on a wall plays the ball's note on its track (harder hit = louder and longer). **Play keys to drop balls** (or tap DROP A BALL on the phone: a random note of the scale), drag on the drum to turn it. K1 SPIN, K2 GRAVITY, K3 SHAPE (3..8 sides), K4 MASS (bounciness). 18 balls at most; a pattern that is recording catches what the balls play |
| PATTERN | (SEQ mode, page 1) per track note pattern on the beat clock, locked to the loop. **Hold SEQ and play keys** to record (overdub: hold again to add), let go and it loops. K1 PATTERN (OFF, PLAY, REC), K2 BARS (1..16; longer adds empty bars, shorter drops the rest), K3 QUANTIZE (1/16 by default; TIMING page K4 SWING 50..75 % pushes every second grid line back, like the OP-1 swing, while quantize is on; applied when the pattern plays, the recorded timing is kept so it can be changed or switched off later), K4 CLEAR. **TAPE LANDS ON** (phone, row above the buttons, shown when a tape exists): ALL fills the loop from its start, or tap bar 1, 2, 3 ... to record one pass of the pattern from that bar (only bars where it fits). The TAPE button lights while it waits for its bar or records; tap it again to cancel. **REC** while the pattern plays records one pass onto the tape (a new loop of the pattern's length when there is none, otherwise it waits for the loop start) and mutes the pattern. The phone shows a piano roll with big rows: tap a cell to add a one-step note, **drag sideways from a cell for a longer note**, drag sideways on a note to stretch or shorten it, tap a note to remove it. The keyboard strip on the left plays the note you touch (drag it to scroll the pitch), the ruler on top scrolls the time when zoomed in. DICE writes a random pattern in the current scale (one undo step). **Bars**: the - and + at the bottom of the roll remove or add a bar at the end (1..16; a new bar is empty, notes behind a removed end are dropped, UNDO brings them back; K2 BARS does the same). UNDO / REDO (16 steps) take back and restore the last edit: a recording pass, a tap on the grid, a clear, a change of the length. **pinch with two fingers** to zoom (fingers apart sideways = zoom in time, apart up and down = fewer, bigger pitch rows; what is between the fingers stays there) or use the TIME -/+ and ROWS -/+ buttons (zoomed in, drag the ruler to scroll, the view follows the playhead), FIT shows where the notes are, GRID sets the cell size (1/4 ... 1/32T, the same as QUANTIZE). Notes go through the track's arpeggiator when it is on. 256 notes per track |
| TAPE | (SEQ mode, page 2) LOOP SPEED (0.25..2x tape-style varispeed of the loop, GLOBAL for all tracks), EXCITER (cassette saturation), LO-FI (bit crush + hiss + dark tone), WOBBLE |
| EDIT | mixer of the selected track: VOLUME, PAN, REVERB SEND, DELAY SEND |
| GLO | (MIX mode, page 2, shortcut) key and scale: SCALE (42: the 7 firmware tables, DORIAN, LYDIAN, LOCR, PENT+, PENT-, BLUES, WHOLE, DIM, PHRDOM, HMAJ, MELMIN, HUNG, HIRA, DHARM, then NEAPM, NEAPJ, PERSN, HUNGJ, LYDDM, ALTRD, LYDAG, DOR#4, SPAN8, INSEN, IWATO, YO, EGYPT, MAJBL, PROMT, ENIGM, OCTWH, LOCR2, MAJ6P, MIN6P, OFF last; the PADS play the notes of the scale, pad 1 = the root), ROOT, OCTAVE, MODE (KEY, CHORD, SNAP: keys outside the scale play the nearest scale note). |
| TEMPO | (SEQ mode, page 3) TEMPO (30..300; locked while a loop exists, the loop defines it), METRONOME (click on the beat clock, accent on the bar), CLICK LEVEL, TAKE SNAP (OFF: the first take sets the tempo; BEAT / BAR: the tempo stays and the take is cut or padded to whole beats / bars, i.e. the loop is quantized) |
| TIMING | (SEQ mode, page 4) SWING (K4, 50..75 %), LIVE QUANTIZE (OFF, 1/4 ... 1/32T: a played key sounds on the nearest grid line of the beat clock, a key that is a little late plays at once; sequencer recording stores the snapped time), PATTERN QUANTIZE (the same setting as on the pattern page), COUNT-IN (OFF, 1 or 2 bars of clicks, with the metronome off too, before a free take or a sequencer recording from a stopped transport; keys played during the count-in do not start the take) |
| SAVE | page LOOP. Tap = save the loop of the selected track into the library, hold 0.7 s = clear ALL loops (every track), also the phone CLEAR button. Knobs: MIC SAMPLE, LOOP SOURCE, SAVE LOOP, MIC THRESHOLD (-46..-6 dB, level that starts a mic take; used by mic sampling and mic loop source) |
| PLAY | play / stop |
| REC | always the tape. While the selected track's pattern plays it records one pass of the pattern onto the tape instead (see PATTERN). Clean looper: arms the first take (it starts with the first key or sound and sets the loop length and tempo; REC again ends it). Loop exists: toggles overdub on the selected track |
| SEL (the button after FX; index 3, the protocol and the firmware call it SCL) | page LENGTH: K1 LENGTH (turn up = loop x2 with its content repeated, down = /2), K2 DUPLICATE, K3 HALVE, K4 bars/bpm (read-only). Needs the firmware built here (`firmware/FM1-Sampler-Workbench.html`): it forwards SCL as button 3 and has no scale screen; the stock/fm1-daw firmware keeps SCL local |
| OCT -/+ | handled by the firmware (octave) |
| keys | KIT algorithm: all 27 keys are pads (note 60 + key, 16 slices repeat over the keys), every key lit, tonality and octave ignored. Otherwise the firmware is kept on a plain chromatic keyboard (scale OFF, SysEx 06 always sends 7; notes 53+key) so keys outside the tonality still play; the app adds the octave, builds triads itself in CHORD mode and lights ONLY keys that belong to the scale (root of each octave blinks; scale OFF: only roots); a sounding key goes dark. |

## Gestures, chops, print FX, themes
Hold a button and use a key, an encoder or the analog knob (HOME, FX, EDIT, GLO and SEL act when let go, unless they were used as a modifier). The phone lists them under MENU > HELP > FM-1 HINTS, and an "FM-1" chip shows on the pages that have some.

| Gesture | Does |
|---|---|
| hold EDIT + key 1-8 | mute that track (add SEL: solo it) |
| hold HOME + key 1-8 | go to that track |
| hold FX + turn PRESETS | browse the effects on the FM-1 screen, let go of FX to open that page with its knob lit |
| hold FX + turn ALGORITHM | the browser lists the whole mix / the selected track |
| hold GLO + turn PRESETS | what the analog knob moves: master volume, track volume, DJ filter, the knob you turned last (all but the master wait until the knob reaches the value) |

**CHOP page** (SOUND tab, last page, KIT/SLICE sample): K1 picks the chop, K2 its start, K3 its end, K4 the step (fine, normal, coarse). REC splits the chop in two, a tap on GLO deletes it, the keys play the pads. The key LEDs show one lit key per chop, the pattern page shows the steps of the bar that plays (the step under the playhead flips), and with EDIT or HOME held keys 1-8 show the tracks.

**PRINT FX** (TAPE tab, last page): by default the tape holds each instrument dry and its effects are applied on every playback, so an effect you play while overdubbing is not kept. Switch PRINT FX on for a track and the tape records the instrument after its cassette and track effects (everything of the track effect pages) and plays back past them, so nothing is applied twice. Switching it on with a tape already on the track runs that tape through the effects once (one undo step). The master effects (reverse, tape stop, the master LIVE pages) and the reverb / delay sends are not printed.

**Themes**: the FM-1 screen uses the colours of the phone's theme (MENU > LOOK).

## Beat clock, sequencer, master fx
- Effects: FX 2 / LIVE FX run on the master, TRACK FX runs one `MasterFx` per track (`active()` skips it at rest values, `feedOnly()` keeps the repeat memory warm).
- The beat clock is the transport position (it restarts with the loop) and runs free while stopped, so the metronome also ticks before a take; the take starts the clock at its first note. Sequencer notes, metronome, trance gate and beat repeat all read it (`engine/dsp/Sequencer.h`: 192 ticks per beat, `tickHits` lists the ticks of an audio block with sample offsets).
- `engine/dsp/MasterFx.h`: the FX 2 and LIVE FX chain (chorus, phaser, width, DJ filter, crusher, gate, repeat, compressor). All of it sits after the sends and before the soft clip; everything is bypassed at its rest value.
- Tests of the added features: `make $PWD/engine/build/test_features` (run from `engine/`), then `engine/build/test_features`.

## Sample sources
- **Look**: PROJECT sheet, VIEW row: four themes (DARK, LIGHT, OP-1 = black screen with the orange accent and the blue / green / white / orange encoder colours, OP-1 BODY = the light grey body with black ink) and FULLSCREEN (hides the status and navigation bars; swipe from the edge to see them for a moment). Both are remembered (`files/settings.txt`).
- **Tape stop** (top bar): hold = the tape runs down while pressed and spins up on release. The FX tab has a STOP page: STOP TIME, SPIN-UP TIME, DEPTH and BUTTON (HOLD, or LATCH: tap to stop, tap to start).
- **Pattern on its own** (SEQ tab, TRIGGER page): PATTERN play/stop (the transport keeps going), KEYS: SOUND or PATTERN (OP-1 style: a key starts the pattern, transposed by that key (on chops of a sample the key changes the PITCH, the chops stay the same); other held keys move it; the last key up stops it), START ON (the beat or the bar), FIRST BEAT earlier/later (slides the whole pattern to line up with the metronome). The pattern page has FULL SCREEN (EXIT to leave) and TO TAPE (one pass of the pattern, transposed, onto the tape).
- **Chop editor** (SOUND tab, SAMPLE page, CHOP EDITOR button above the waveform; it switches the track to KIT/SLICE): the whole screen for cutting a sample into pads. Every chop has its own START and END flag: drag one and only that edge moves (the neighbours stay). Select a chop with its number under the waveform (tapping a flag does not select), tap the empty waveform to cut a new chop, DELETE CHOP removes the selected one (two stay). AUTO CHOP cuts at the hits of the sound (the start is the beginning of the attack, the end is where the hit has died away), EVEN CHOP in equal parts, CHOPS (4, 8, 12, 16, 24, 32) and SENS (LOW / MID / HIGH, how soft a hit may be) feed both. The numbers under the waveform play each chop. **KEY: HOLD** (a chop sounds while the key is held and releases within 20 ms: chops ignore the ENV attack and release) or **KEY: WHOLE CHOP** (a key plays the chop to its end, whatever the length of the press). Saved with the project.
- **New loop** (TAPE tab, NEW LOOP): TAPE (OP-1 style, a first take of 1 to 64 bars that ends by itself; TAKE in the top bar also cycles FREE, 1 ... 64) or SEQUENCER (a pattern of 1 to 16 bars that records the keys and loops, edited on the PATTERN page). **METRO** in the top bar is the click, from any page. **NEW PROJECT** (MENU, or PROJECTS) saves what is open and starts clean.
- **Projects**: the PROJECT button in the top bar. Everything goes into `files/projects/<name>/`: settings, patterns, which sample or synth each track uses, the tape audio (`tape_N.wav`, `mix.wav`). SAVE, SAVE AS NEW, LOAD, DELETE. The session is also kept as `autosave` every 20 s and when the app goes to the background, and the app comes back to it at the next start (if the last restore never finished, it is skipped once). EXPORT MIDI (patterns, one track each) and EXPORT MIX (WAV of the loop) open the share sheet; IMPORT MIDI FILE puts the notes of a .mid on the selected track and the ones after it. TAPE STOP (top bar) is a hold button: the tape runs down while it is held and spins up on release.
- **Built-in synths** (`engine/dsp/Synth.h`, library group *Synths*, no sample needed, each brings its own envelope): Sub Bass, Saw Bass, Acid, FM Bass, Mono Lead, Pulse Lead, Chip Square, Super Saw, Warm Pad, String Ens, Glass Pad, Brass, Pulse Pad, FM Piano, FM Bell, Organ, Pluck, Harp, Muted Bass, Wind, Zap Kick, Laser Zap, Hoover. Subtractive (oscillators, unison stack, sub, noise, resonant filter with envelope), 2-operator FM, Karplus-Strong strings, a wavetable (8 morphing frames: Wave Pad, Wave Lead, Vocal Wave, Wave Bass), modal bars and bells (Marimba, Vibes, Glockenspiel, Tubular Bell, Kalimba) and three synthesized drum kits (Synth Kit 808, Punch, Lo-Fi: 16 pads each); the ENV, LFO, FX and sequencer pages work on them like on a sample. Add one by adding a row to `kSynths`. Added in v0.43: 39 more patches (basses, stabs, leads, pads, wavetable bells and pads, struck bars and bowls, plucked guitars and an upright bass, 909 and trap drum kits) and a **phase distortion** engine (the Casio CZ idea: a cosine read through a bent phase, with a saw, square, pulse and resonant shape; the PD patches). On the SYNTH OSC page a PD patch reads the WAVE buttons as its own shapes (SINE = saw, SAW = square, SQUARE = pulse, TRIANGLE = resonant). New patches are only ever appended, because a saved project names a synth by its number.
- **Bundled library** (`samples/`, packed into the APK assets by Gradle, copied to `files/samples` on the first start and again after every update): Bass, Drums (loaded as KIT), Keys, Leads, Pads. 49 sounds. 30 of them are synthesized by `tools/gen_instruments.py` (numpy / scipy, deterministic, no recordings): Keys: E-Piano, Wurly, Organ, Marimba, Music Box, Bell, Harpsichord, Clav, Vibraphone. Bass: 808 Sub, Saw Bass, Square Bass, Pluck Bass, Reese, Funk Bass. Pads: Strings, Choir, Warm Pad, Glass Pad, Brass Pad, Dark Pad. Leads: Square Lead, Saw Lead, Chip, Flute, Pluck Lead, Sync Lead. Drums: 808 Kit, Electro Kit, Lo-Fi Kit (16 hits each, one per pad). Two more drum kits are cut from real recordings, *Big Rusty Drums* by Karoryfer Samples (CC0 1.0, https://github.com/sfzinstruments/karoryfer.big-rusty-drums): **Rusty Kit** (a dry, rusty acoustic kit as recorded) and **Boom Bap Kit** (the same hits through an SP-1200 style chain: saturation, 26 kHz hold, 12 bit, low pass); `tools/build_rusty_kits.py` downloads the hits and rebuilds both. Melodic samples are tuned to C4 (basses to C2): they play at their own pitch on MIDI note 60. Run `python3 tools/gen_instruments.py` to rebuild them. Generated by `tools/gen_instruments.py` (`--only=Name,Name` renders just some); v0.43 adds Celesta, Toy Piano, Accordion, Moog Bass, Wobble Bass, Dub Sub, Vapor Pad, Ice Pad, Theremin, Formant Lead and a 909 Kit.
- **Microphone**: phone button "RECORD FROM MIC" or LOOP page K1. Waits for sound above the trigger level, trims and normalises, saves `sample_<ms>.wav` in the data dir, loads it on the track it was started on.
- **Disk**: phone button "LOAD FILE FROM DISK" -> system picker -> `AudioImport.java` converts to a 16-bit WAV (`sample_<name>.wav` in `files/`): WAV 8/16/24/32/float directly, anything else through MediaCodec (mp3, aac, ogg, flac). Max 90 s.
- **Saved loops** and the two above all live in the "User" group of the library (`sample_*.wav` in the data dir).
Browsing replaces the sample of the selected track; a new sampler keeps the track's settings (`applyAll` after `sampleVersion` bumps).
At start (and after NEW PROJECT): every track is empty, no sound is loaded. Pick one from the library, a built-in synth, the mic or a file. NEW PROJECT (MENU, or PROJECTS) saves what is open and starts clean.

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
- `engine/dsp/Reverb.h` (8 combs + 4 allpasses per side), `TapeDelay.h`, `TapeInsert.h`, `Arp.h` (UP, DOWN, UPDN, CONV, RAND, PLAYED (the order the keys went down in), TOMBOLA (random notes with random rests) over 1..4 octaves, sample accurate gate), `Wav.*`.
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

## v0.45

- **FM6** (Felucca's six-operator FM, msfa / Dexed core) with its 15 factory patches.
- **Felucca-Engines** (MIT, Leo Kuroshita), float ports: **ANALOG** (saw/square/tri/sine/PWM, hard sync, sub), **TRIO** (three oscillators, ring mod, sync, multimode filter), **LOFI** (chip voice: 4/8/1-bit, wave RAM, stepped envelope, sweep, arp) and **NOISE** (analog, dust, shift-register and metallic noise). 28 patches.
- **DRUM-X** (JIANT, GPL-3.0): nine synthesised kits. Pads 0-7 are the eight sounds on patch A, pads 8-15 the same on patch B.
- **ORACLE** (Tessera, GPL-3.0, Theo Dainis): a Prophet-5 style voice (two oscillators, Poly-Mod, four-pole ladder, sync, VINTAGE drift) with its 24 sounds.
- **Pattern generators** on the pattern page (button beside DICE, one press = the next generator): TURING, EUCLID, MARKOV, MORPH, AWAKE, POLY, BOUNCE (ideas from Tessera) and ACID (a 303 line with accents and slides, after X0X's TB-3PO). Seeded, in the current scale.
- 253 synths in all. Not ported yet: Felucca GRAIN and PHYS, JIANT STRING, the break player, Tessera's EVOLVE / FORM / cords, and SID.

## v0.44

Vinyl scratch effect (master FX 6, TRACK FX 7); 103 more synth patches (74 -> 177) and 7 more library instruments, from the other open FM-1 firmwares: GHOULBOX (hurdy-gurdy, tonewheel organ and formant voice engines, dungeon patches, CC0 instrument samples), SLOOP (32 synthesised drum kits), X0X (TB-303, TR-808, TR-909), JIANT FM (ANIMAL: bird, insect, frog and mammal calls). fm1-sampler is now GPL-3.0-only (below). Not heard on a phone or an FM-1 yet.

## License and credits

fm1-sampler is **GPL-3.0-only** (see `LICENSE`) since v0.44, because it contains ports of GPL code:
- **GHOULBOX** (https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth, GPL-3.0-only): the GURDY, WHEEL and VOICE synth engines (`engine/dsp/DungeonSynth.h`, float ports) and its dungeon patches. GHOULBOX is a fork of **Felucca** by Leo Kuroshita / Hugelton Instruments (https://github.com/hugelton/Felucca, GPL-3.0-only).
- **SLOOP** (https://github.com/isod89/sloop-fm1, GPL-3.0-only, also on Felucca): the 32 synthesised drum kits (`engine/dsp/SloopDrum.h`, the float port of its drum voice; `engine/dsp/SloopKits.h`, generated by `tools/gen_sloop_kits.py` from SLOOP's `tools/gen_drumkits.py`).
- Library instruments from GHOULBOX's CC0 sample sets (Grand Piano, Folk Harp, Renaissance Organ, Real Flute, Recorder, Tenor Sax, Bowed Psaltery): Versilian Studios VSCO-2 CE and VCSL (Sam Gossner), CC0 1.0. `tools/import_ghoulbox_samples.py` rebuilds them.

- **X0X** (https://github.com/charlesvestal/fm1-x0x, GPL-3.0-only, on Felucca): its DSP is C99 float and is used as is (`engine/dsp/bass303.cpp`, `drum808.cpp`, `drum909.cpp` and their headers): the TB-303 after Open303 (Robin Schmidt, MIT) with Devilfish ranges and RAT drive (engine 9, `Acid303.h`), the TR-808 after 8W8 (athousanddetails; rim shot after sc808, MIT) (engine 10, `Tr808Voice.h`), the TR-909 after 9W9 / ER-99 (athousanddetails, Matthew Cieplak) with ER-99's hat, ride and crash recordings as mu-law data in `x0x_drum_samples.h` (engine 11, `Tr909Voice.h`). Those engines run at 44.1 kHz; on another host rate the drums are resampled and the 303 is retuned.
- **JIANT FM** (https://github.com/juanjiant-bit/JIANT, GPL-3.0-only, by Juan Ignacio Antonucci, on Felucca): the ANIMAL engine, as a float port (`engine/dsp/AnimalSynth.h`, engine 12): CMOS bird machine, cricket / frog pulses, songbird syrinx, wingbeats, mammal larynx, 24 patches.

New synth patches (74 -> 177): GURDY, WHEEL and VOICE engines with 23 patches, 14 dungeon sounds on the oscillator / string engines, 32 "Sloop" drum kits, 8 TB-303 patches, the X0X 808 and 909 kits, and 24 ANIMAL patches. Not ported yet: Felucca's own engines (6-operator FM, TRIO, ANALOG, GRAIN, PHYS, LOFI, NOISE), JIANT's DRUM-X, STRING and sung words, X0X's TB-3PO acid-line generator and break player.
