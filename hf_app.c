
/* Copyright 2026
 * HF Receiver Overlay App for F4HWN (SI4732)
 * Based on the official FM overlay app by Armel F4HWN
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app_api.h"
#include "si4732_minimal.h"

/* ------------------------------------------------------------------ */
/*  Configuration                                                     */
/* ------------------------------------------------------------------ */
#define STEP_KHZ        1          /* default step in kHz             */
#define SCAN_SETTLE     80         /* ms settle time after tune       */
#define CHMAX           20         /* fewer memories than FM (RAM!)   */

/* ------------------------------------------------------------------ */
/*  Global state (same style as fm_app.c)                             */
/* ------------------------------------------------------------------ */
struct globals {
    /* Radio state */
    uint16_t freq_kHz;             /* currently playing               */
    uint16_t sel_freq;             /* VFO selected frequency          */
    uint8_t  band;                 /* index into si_bands[]           */
    bool     is_mr;                /* Memory mode                     */
    uint8_t  sel_ch;               /* selected memory channel         */

    /* UI / input */
    uint8_t  chPos, savePos, inIdx;
    int8_t   scanState;            /* 0 = off, +1 up, -1 down         */
    bool     autoScan, foundFreq, askSave, askDelete, fArm, running;
    uint8_t inBox[5];
    uint16_t lo, hi;               /* current band limits             */
    uint16_t scanTimer;

    const app_api_t *api;
    uint16_t mem[CHMAX];           /* simple memory (no EEPROM yet)   */
};

static struct globals g;
#define A (g.api)

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */
static void bandLimits(void)
{
    g.lo = si_bands[g.band].lo_kHz;
    g.hi = si_bands[g.band].hi_kHz;
}

static uint16_t wrap(uint16_t f)
{
    if (f < g.lo) return g.hi;
    if (f > g.hi) return g.lo;
    return f;
}

static void tune(void)
{
    SI4732_SetFrequency(g.freq_kHz);
}

static bool validCh(uint8_t c)
{
    return c < CHMAX && g.mem[c] >= g.lo && g.mem[c] <= g.hi;
}

static uint8_t findNext(uint8_t c, int8_t dir)
{
    for (uint8_t i = 0; i < CHMAX; i++) {
        if (c == 0xFF) c = CHMAX - 1;
        else if (c >= CHMAX) c = 0;
        if (validCh(c)) return c;
        c = (uint8_t)(c + dir);
    }
    return 0xFF;
}

static int configChannel(void)
{
    g.freq_kHz = g.sel_freq;
    if (g.is_mr) {
        uint8_t c = findNext(g.sel_ch, +1);
        if (c == 0xFF) {
            g.is_mr = false;
            return -1;
        }
        g.sel_ch = c;
        g.freq_kHz = g.mem[c];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Drawing                                                           */
/* ------------------------------------------------------------------ */
static void freqBig(uint16_t f)
{
    char s[12];
    char *o = s;

    if (f >= 10000) {                 /* 5 digits (e.g. 14100) */
        o = A->append_u32(o, f);
    } else if (f >= 1000) {           /* 4 digits + kHz */
        o = A->append_u32(o, f);
    } else {
        o = A->append_u32(o, f);
    }
    *o = '\0';

    A->display_freq(s, 20, 1, true);  /* adjust x-position if needed */
}

static void draw(void)
{
    char b[16];

    A->display_clear();
    A->status_clear();
    A->draw_battery();

    A->print_tiny("HF", 0, 0, false, true);

    /* Status line */
    if (g.askSave) {
        A->print_string("SAVE?", 0, 127, 3, 8);
    } else if (g.askDelete) {
        A->print_string("DEL?", 0, 127, 3, 8);
    } else if (g.scanState) {
        A->print_string(g.autoScan ? "A-SCAN" : "M-SCAN", 0, 127, 3, 8);
    } else if (g.is_mr) {
        char tmp[8];
        char *p = tmp;
        p = A->append_u32(p, g.sel_ch + 1);
        *p = '\0';
        A->print_string(tmp, 0, 40, 3, 8);
        A->print_string("MR", 50, 127, 3, 8);
    }

    /* Frequency / input */
    if (g.inIdx > 0) {
        /* simple digit entry display */
        for (uint8_t k = 0; k < 5; k++) {
            b[k] = (k < g.inIdx) ? (char)('0' + g.inBox[k]) : '-';
        }
        b[5] = '\0';
        A->print_string(b, 0, 127, 1, 10);
    } else {
        freqBig(g.freq_kHz);
    }

    /* AM RSSI is reported in dBuV; convert to dBm for a 50-ohm input. */
    int16_t rssiDbm = (int16_t)SI4732_GetRSSI() - 107;
    char rssiText[12];
    char *r = A->append_i32(rssiText, rssiDbm);
    *r++ = ' ';
    *r++ = 'd';
    *r++ = 'B';
    *r++ = 'm';
    *r = '\0';
    A->print_tiny(rssiText, 0, 56, false, true);
}

static void show(void)
{
    draw();
    A->blit_status();
    A->blit_full();
}

/* ------------------------------------------------------------------ */
/*  Scan (simplified)                                                 */
/* ------------------------------------------------------------------ */
static void scanTuneNext(void)
{
    g.foundFreq = false;
    g.freq_kHz = wrap((uint16_t)(g.freq_kHz + g.scanState * si_bands[g.band].step_kHz));
    tune();
    g.scanTimer = SCAN_SETTLE;
}

static void stopScan(void)
{
    if (g.autoScan) {
        g.is_mr = true;
        g.sel_ch = 0;
    } else if (!g.is_mr) {
        g.sel_freq = g.freq_kHz;
    }
    g.scanState = 0;
    g.autoScan = false;
    configChannel();
    tune();
}

static void scanStep(void)
{
    uint8_t rssi = SI4732_GetRSSI();
    if (rssi > 25) {                      /* simple squelch threshold */
        if (!g.autoScan) {
            g.scanState = 0;
            g.foundFreq = true;
            if (!g.is_mr) g.sel_freq = g.freq_kHz;
            return;
        }
        if (g.chPos < CHMAX) {
            g.mem[g.chPos++] = g.freq_kHz;
        }
        if (g.chPos >= CHMAX) {
            stopScan();
            return;
        }
    }
    scanTuneNext();
}

static void startManual(int8_t dir)
{
    if (g.scanState) {
        stopScan();
        return;
    }
    g.autoScan = false;
    g.chPos = 0;
    g.scanState = dir;
    scanTuneNext();
}

static void startAuto(void)
{
    if (g.scanState) {
        stopScan();
        return;
    }
    g.autoScan = true;
    g.chPos = 0;
    g.scanState = +1;
    for (uint8_t i = 0; i < CHMAX; i++) g.mem[i] = 0xFFFF;
    g.freq_kHz = g.lo;
    tune();
    g.scanTimer = SCAN_SETTLE;
}

/* ------------------------------------------------------------------ */
/*  Digit entry                                                       */
/* ------------------------------------------------------------------ */
static void digit(uint8_t d)
{
    if (g.askDelete || g.scanState) return;

    if (g.askSave || g.is_mr) {
        /* 2-digit channel */
        if (g.inIdx < 2) g.inBox[g.inIdx++] = d;
        if (g.inIdx >= 2) {
            uint8_t c = (uint8_t)(g.inBox[0] * 10 + g.inBox[1] - 1);
            g.inIdx = 0;
            if (g.askSave) {
                if (c < CHMAX) g.savePos = c;
            } else if (validCh(c)) {
                g.sel_ch = c;
                g.freq_kHz = g.mem[c];
                tune();
            }
        }
        return;
    }

    /* VFO frequency entry (up to 5 digits) */
    if (g.inIdx < 5) g.inBox[g.inIdx++] = d;

    if (g.inIdx >= 4) {                   /* accept after 4 or 5 digits */
        uint32_t f = 0;
        for (uint8_t i = 0; i < g.inIdx; i++) {
            f = f * 10 + g.inBox[i];
        }
        g.inIdx = 0;
        if (f >= g.lo && f <= g.hi) {
            g.sel_freq = (uint16_t)f;
            g.freq_kHz = (uint16_t)f;
            tune();
        }
    }
}

/* ------------------------------------------------------------------ */
/*  MENU / UP-DOWN / Band / VFO-MR                                    */
/* ------------------------------------------------------------------ */
static void menu(void)
{
    g.inIdx = 0;
    if (!g.is_mr) {
        if (g.askSave) {
            g.mem[g.savePos] = g.freq_kHz;
        }
        g.askSave = !g.askSave;
    } else {
        if (g.askDelete) {
            g.mem[g.sel_ch] = 0xFFFF;
            configChannel();
            tune();
        }
        g.askDelete = !g.askDelete;
    }
}

static void upDown(int8_t step)
{
    if (g.scanState) {
        stopScan();
        return;
    }

    if (g.is_mr) {
        uint8_t next = g.sel_ch;
        if (step < 0 && next == 0) next = 0xFF;
        else next = (uint8_t)(next + step);
        next = findNext(next, step);
        if (next != 0xFF) {
            g.sel_ch = next;
            g.freq_kHz = g.mem[next];
            tune();
        }
        return;
    }

    g.freq_kHz = wrap((uint16_t)((int32_t)g.freq_kHz +
                                 step * si_bands[g.band].step_kHz));
    g.sel_freq = g.freq_kHz;
    tune();
}

static void toggleMr(void)
{
    g.inIdx = 0;
    g.is_mr = !g.is_mr;
    if (configChannel() == 0) tune();
}

static void nextBand(void)
{
    g.band++;
    if (g.band >= si_band_count) g.band = 0;
    bandLimits();
    if (g.freq_kHz < g.lo || g.freq_kHz > g.hi)
        g.freq_kHz = g.lo;
    g.sel_freq = g.freq_kHz;
    g.is_mr = false;
    tune();
}

static void handleKey(uint8_t key)
{
    if (key == APP_KEY_F) {
        g.fArm = true;
        return;
    }

    bool fn = g.fArm;
    g.fArm = false;

    if (!fn && key <= APP_KEY_9) {
        digit(key);
        return;
    }

    switch (key) {
        case APP_KEY_MENU:
            menu();
            break;
        case APP_KEY_UP:
            if (fn) startManual(+1);
            else upDown(+1);
            break;
        case APP_KEY_DOWN:
            if (fn) startManual(-1);
            else upDown(-1);
            break;
        case APP_KEY_1:
            if (fn) nextBand();
            else digit(1);
            break;
        case APP_KEY_3:
            if (fn) toggleMr();
            else digit(3);
            break;
        case APP_KEY_STAR:
            if (fn) startAuto();
            else startManual(+1);
            break;
        case APP_KEY_EXIT:
            if (g.scanState) stopScan();
            else if (g.inIdx || g.askSave || g.askDelete) {
                g.inIdx = 0;
                g.askSave = false;
                g.askDelete = false;
            } else {
                g.running = false;
            }
            break;
        default:
            if (!fn && key <= APP_KEY_9) digit(key);
            break;
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    if (!api) return;

    g.api = api;
    g.band = (si_band_count > 4) ? 4 : 0;
    bandLimits();
    g.freq_kHz = g.lo;
    g.sel_freq = g.freq_kHz;
    g.running = true;
    for (uint8_t i = 0; i < CHMAX; i++) g.mem[i] = 0xFFFF;

    A->led(false);
    A->backlight_on();

    if (!SI4732_Init()) {
        A->display_clear();
        A->status_clear();
        A->print_string("SI4732 ERROR", 0, 127, 2, 8);
        A->blit_status();
        A->blit_full();
    } else {
        tune();
        show();
    }

    uint8_t lastKey = APP_KEY_INVALID;
    uint16_t batteryTimer = 100;
    while (g.running) {
        bool redraw = false;
        uint8_t key = A->get_key();

        if (key == APP_KEY_SAVER) {
            lastKey = APP_KEY_INVALID;
        } else if (key == APP_KEY_WAKE) {
            lastKey = APP_KEY_INVALID;
            redraw = true;
        } else if (key == APP_KEY_INVALID) {
            lastKey = APP_KEY_INVALID;
        } else if (key != lastKey) {
            handleKey(key);
            lastKey = key;
            redraw = true;
        }

        if (g.scanState) {
            if (g.scanTimer <= 10) {
                g.scanTimer = 0;
                scanStep();
                redraw = true;
            } else {
                g.scanTimer -= 10;
            }
        }

        if (batteryTimer <= 10) {
            batteryTimer = 100;
            A->battery_sample();
            redraw = true;
        } else {
            batteryTimer -= 10;
        }

        if (redraw) show();
        A->backlight_update();
        A->delay_ms(10);
    }

    SI4732_PowerDown();
    A->led(false);
}
