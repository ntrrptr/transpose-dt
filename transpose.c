/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TRANSPOSE 0.6.3 for the Digitakt mk1 (seq-events 0.2, core 4.1).
 *
 * Hold TRK + NO: an overlay opens on the selected track. Two values are kept:
 *   RANGE    the keyboard's octave, set with UP / DOWN (+-12 semitones per press,
 *            up to +-4 octaves). Changing it does not move the transpose by itself.
 *   OFFSET   the transpose actually played, in semitones (-48..+48).
 * TRIG n sets OFFSET = RANGE + (n - 9): TRIG 9 is the range's base, TRIG 1 is
 * 8 semitones below it, TRIG 16 is 7 above. Knob H moves OFFSET +-1 per detent.
 * YES commits (TRK + NO + YES): the offset is written into the track's note-locks
 * and OFFSET goes back to 0. RANGE is kept. Releasing TRK or NO closes the overlay.
 *
 * Only notes[0] moves. COMMIT writes into the pattern, so it survives power-off;
 * OFFSET and RANGE are RAM only.
 *
 * Drawing uses direct calls to fw_fillrect and fw_textf, the way 0.2 and 0.3.1
 * did. Casting them through function pointers drops their imports from the .elemod.
 *
 * Unverified, from digiseq_dt.c and the 1.53 build:
 *   NO_LOCK    0xff = no note-lock on a step (the 255 digiseq uses for "none")
 */
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

/* ---- Keys (digiseq_dt.c) ------------------------------------------------ */
#define KEY_FUNC   1
#define KEY_TRK    2
#define KEY_NO     13
#define KEY_YES    12
#define KEY_UP     14
#define KEY_DOWN   15
#define KEY_STEP1  24          /* TRIG 1 .. TRIG 16 = KEY_STEP1 .. KEY_STEP1 + 15 */

#define HOLD_A     KEY_TRK
#define HOLD_B     KEY_NO

/* ---- Encoders: knobs A-H are 1-8 (digiseq_dt.c) ------------------------- */
#define KNOB_H     8
#define COUNTS     4           /* raw counts per detent (as digiseq) */

/* ---- Limits ------------------------------------------------------------- */
#define OFFSET_MAX 48
#define OCT_MAX    4
#define SEMI_BASE  9           /* TRIG 9 = the range's base */

/* ---- Pattern (layout from digiseq_dt.c, OS 1.53) ------------------------ */
/* DSQ_PATTERN_PP is set by cflags in mod.json (0x4199dc38 for 1.53). */
#define PATTERN      (*(uint8_t *const volatile *)DSQ_PATTERN_PP)
#define TRACK_STRIDE 911
#define TRACK_OF(pat, t) ((pat) + TRACK_STRIDE * (t))
#define TR_NOTE_LOCK 0x280      /* step's note-lock byte at +0x280 */
#define W_TRIG       0x0001     /* step word, bit 0 = trig (big-endian u16 at +0) */
#define NO_LOCK      0xff

/* ---- ev_key / ev_enc accessors ------------------------------------------ */
#define EV_KEY(ev)   (*(unsigned long *)((char *)(ev) + 12))
#define EV_FLAGS(ev) (*(unsigned long *)((char *)(ev) + 16))
#define EV_ENC(ev)   (*(unsigned long *)((char *)(ev) + 12))
#define EV_DELTA(ev) (*(long *)((char *)(ev) + 16))
#define F_DOWN     1
#define F_REPEAT   8

/* ---- State -------------------------------------------------------------- */
static volatile int8_t offset[SQ_TRACKS];  /* the transpose the sequencer plays */
static int8_t range[SQ_TRACKS];            /* -4..+4: the keyboard's octave */
static long    knob_acc[SQ_TRACKS];        /* raw counts for knob H */
static unsigned char held_a, held_b;
static int open_t = -1;                    /* -1 = closed */
static int last_commit = -1;               /* steps written by the last COMMIT */

static int clampi(long v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : (int)v);
}

/* COMMIT: each step with a trig gets its note-lock set to its current note plus
 * the offset (a step without a lock plays the track's note). Then the offset is
 * reset to 0. Returns the steps written, or -1 when there is nothing to do. */
static int commit(int t)
{
    uint8_t *pat = PATTERN, *trk;
    struct sq_track info;
    int len, s, changed = 0;

    if (!pat || t < 0 || t >= SQ_TRACKS || offset[t] == 0)
        return -1;
    if (sq_track_info(t, &info, sizeof info) != 0)
        return -1;
    len = info.len;
    if (len < 1 || len > SQ_STEPS)
        return -1;

    trk = TRACK_OF(pat, t);
    for (s = 0; s < len; s++) {
        unsigned w = (unsigned)(trk[2 * s] << 8 | trk[2 * s + 1]);
        int cur, n;
        if (!(w & W_TRIG))
            continue;
        cur = trk[TR_NOTE_LOCK + s];
        if (cur == NO_LOCK)
            cur = info.note;
        n = clampi((long)cur + offset[t], 0, 127);
        trk[TR_NOTE_LOCK + s] = (uint8_t)n;
        changed++;
    }

    offset[t] = 0;
    knob_acc[t] = 0;
    sq_rearm(1u << t);
    return changed;
}

/* ---- ev_key (order 30, before digiseq's 40) ----------------------------- */
int transpose_key(void *brain, void *ev)
{
    unsigned long k     = EV_KEY(ev);
    unsigned long flags = EV_FLAGS(ev);
    int down = (int)((flags & F_DOWN) && !(flags & F_REPEAT));
    (void)brain;

    if (k == HOLD_A || k == HOLD_B) {
        if (!(flags & F_REPEAT)) {
            unsigned char pressed = (unsigned char)((flags & F_DOWN) != 0);
            if (k == HOLD_A) held_a = pressed;
            else             held_b = pressed;
        }
        if (held_a && held_b) {
            if (open_t < 0) {
                uint32_t t = fw_active_track;   /* variable 0-7, read as digiseq_dt.c does */
                if (t < SQ_TRACKS) {
                    open_t = (int)t;
                    last_commit = -1;
                }
            }
        } else {
            open_t = -1;
        }
        return 0;
    }

    if (open_t < 0)
        return 0;

    /* TRIG n: OFFSET = 12 * RANGE + (n - 9). */
    if (k >= KEY_STEP1 && k < KEY_STEP1 + 16) {
        if (down) {
            int n = (int)(k - KEY_STEP1) + 1;
            offset[open_t] = (int8_t)clampi(12L * range[open_t] + (n - SEMI_BASE),
                                            -OFFSET_MAX, OFFSET_MAX);
        }
        return 1;
    }
    /* UP / DOWN: move the keyboard's range, not the transpose. */
    if (k == KEY_UP || k == KEY_DOWN) {
        if (down)
            range[open_t] = (int8_t)clampi((long)range[open_t] + (k == KEY_UP ? 1 : -1),
                                           -OCT_MAX, OCT_MAX);
        return 1;
    }
    if (k == KEY_YES) {
        if (down && held_a && held_b)
            last_commit = commit(open_t);
        return 1;
    }
    return 0;
}

/* ---- ev_enc (order 30): knob H while the overlay is open ----------------- */
int transpose_enc(void *brain, void *ev)
{
    unsigned long e = EV_ENC(ev);
    long acc;
    int t = open_t;
    (void)brain;

    if (t < 0 || e != KNOB_H)
        return 0;

    knob_acc[t] += EV_DELTA(ev);
    acc = knob_acc[t] / COUNTS;
    knob_acc[t] -= acc * COUNTS;
    if (acc == 0)
        return 1;

    offset[t] = (int8_t)clampi((long)offset[t] + acc, -OFFSET_MAX, OFFSET_MAX);
    return 1;
}

/* ---- ev_draw (order 95, after digiseq's 90) ----------------------------- */
/* Direct calls only: see the header. The frame is four 1-pixel rectangles. */
void transpose_draw(void *bmp, void *ctrl)
{
    int t = open_t;
    (void)ctrl;
    if (t < 0)
        return;
    fw_fillrect(bmp,   4,   4, 123,  52, 0);   /* background */
    fw_fillrect(bmp,   4,   4, 123,   4, 1);   /* bottom edge */
    fw_fillrect(bmp,   4,  52, 123,  52, 1);   /* top edge */
    fw_fillrect(bmp,   4,   4,   4,  52, 1);   /* left edge */
    fw_fillrect(bmp, 123,   4, 123,  52, 1);   /* right edge */
    fw_textf(bmp, fw_font5, 8, 44, -1,
             t < SQ_AUDIO ? "TRANSPOSE T%d" : "TRANSPOSE MIDI %d", (t & 7) + 1);
    fw_textf(bmp, fw_font5, 8, 32, -1, "RANGE %+d OCT", (int)range[t]);
    fw_textf(bmp, fw_font5, 8, 20, -1, "OFFSET %+d ST", (int)offset[t]);
    if (last_commit >= 0)
        fw_textf(bmp, fw_font5, 8, 9, -1, "COMMITTED %d STEPS", last_commit);
    else
        fw_textf(bmp, fw_font5, 8, 9, -1, "TRIG9=BASE  H=DIAL");
}

/* ---- ev_fire_notes (order 50) ------------------------------------------- */
void transpose_notes(int32_t track, int32_t step, int8_t notes[SQ_NOTES])
{
    int32_t n;
    (void)step;
    if (track < 0 || track >= SQ_TRACKS || offset[track] == 0)
        return;
    if (notes[0] < 0)
        return;
    n = (int32_t)notes[0] + (int32_t)offset[track];
    if (n <   0) n =   0;
    if (n > 127) n = 127;
    notes[0] = (int8_t)n;
}
