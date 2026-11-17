# TRANSPOSE for Digitakt mk1

A small [elekloader](https://github.com/irpina/elekloader) mod that transposes the
notes of a track while you play. You can audition a transpose live, and only write it
into the pattern when you decide to keep it.

## The two values

The overlay shows two numbers:

- **RANGE** — the octave the TRIG pads refer to. It is `-4` to `+4`. Changing RANGE
  does **not** move the notes by itself; it only rebases what the TRIG pads mean.
- **OFFSET** — the transpose actually played, from `-48` to `+48` semitones.

Both live in RAM while the machine is on. Only **COMMIT** writes anything into the
pattern, and only COMMIT survives power-off.

## Opening and closing the overlay

| Buttons | What happens |
|---|---|
| Hold **TRK + NO** together | Opens the overlay for the currently selected track. Hold both keys down the whole time you are using the controls below. |
| Release **TRK** or **NO** | Closes the overlay. This does **not** undo the transpose — OFFSET keeps playing until you change it or commit it. |

## Controls while the overlay is open

| Control | What it does |
|---|---|
| **UP** | RANGE +1 octave (up to `+4`). The played transpose does not change. |
| **DOWN** | RANGE −1 octave (down to `−4`). The played transpose does not change. |
| **TRIG 9** | Sets OFFSET to the base of the current RANGE (`RANGE × 12` semitones). |
| **TRIG 1 … TRIG 8** | Sets OFFSET below the base: TRIG 1 = base − 8, TRIG 8 = base − 1 semitones. |
| **TRIG 10 … TRIG 16** | Sets OFFSET above the base: TRIG 10 = base + 1, … TRIG 16 = base + 7 semitones. |
| **Knob H** | Nudges OFFSET by one semitone per detent (up or down). This is the fine tune after picking a TRIG pad. |
| **YES** (while still holding TRK + NO) | **COMMIT.** Writes OFFSET into the note-locks of every step on the track that has a trig, resets OFFSET to 0, keeps RANGE, and shows `COMMITTED n STEPS`. |

Notes are clamped to the range 0–127. Only the step's **main** note moves; other
note-locks are left alone. Steps without a trig are not changed.

### Example

1. Select audio track 1.
2. Hold **TRK + NO**. The screen shows `RANGE +0 OCT`, `OFFSET +0 ST`.
3. Press **TRIG 12** → OFFSET becomes `+3` semitones. The track now plays 3 semitones up.
4. Turn **knob H** two detents right → OFFSET `+5`.
5. Press **YES** → those 5 semitones are written into the pattern's note-locks, and
   OFFSET returns to `0`. The transpose is now permanent in the pattern.
6. Release **TRK** or **NO** to return to the normal screen.

If you want to try without saving, skip step 5 and just release TRK or NO. To remove
an uncommitted transpose, set OFFSET back to 0 (press **TRIG 9** with RANGE `+0`, or
turn knob H until it reads `+0 ST`).

## Supported firmware

- Digitakt mk1, OS **1.53** and **1.54**
- Requires **core 4.1** and **seq-events 0.2**
- Stock tracks only

## Build it yourself

You need Python 3.9+, git and the m68k cross compiler. With the stock Elektron OS file
(this repository never contains it):

```sh
python -m elekloader.sdk.build . --stock Digitakt_OS1.53.syx
python -m elekloader.sdk.build . --stock Digitakt_OS1.54.syx
```

The built `.elemod` files appear in `out/`. To check them:

```sh
python -m elekloader.lint out/transpose-dt-<version>.elemod \
  --stock Digitakt_OS1.53.syx \
  --with core-4.1-os1.53.elemod --with seq-events-dt-0.2-os1.53.elemod
```

## Flash, and get back to stock

Build a firmware file from your stock OS and the `.elemod` on the
[elekloader site](https://elekloader.github.io/elekloader/), or with
`python -m elekloader.patch`. Send the resulting `.syx` with Elektron Transfer like a
normal OS update. The bootloader is never changed.

To go back to stock: hold **FUNC** while powering on the Digitakt, press **TRIG 4**
(OS UPGRADE), then send the original Elektron `.syx`. Do not press TRIG 2 or TRIG 3 —
those are for an empty/factory reset.

## Test status

The `.elemod` files are built and lint-clean. Hardware test details are listed in the
shop submission for this mod.

## Licence

GPL-2.0-or-later. No Elektron firmware or Elektron code is included.
