
/* Copyright 2026 Xserv
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
#define SCAN_SETTLE     50         /* ms settle time after tune       */
#define SCAN_RSSI_THRESHOLD 17     /* approximately -90 dBm (RSSI - 107) */
#define INPUT_TIMEOUT_MS 1000
#define F_KEY_TIMEOUT_MS 500
#define CHMAX           10         /* shared memory/preset slots      */
#define WAVE_SAMPLES    32
#define WAVE_PERIOD_CYCLES 6000u   /* 8 kHz at 48 MHz                  */

static const uint8_t tuneSteps[] = { 12, 10, 9, 5, 2, 1 };
static const char tuneStepLabels[][6] = {
    "12kHz", "10kHz", "9kHz", "5kHz", "2kHz", "1kHz"
};

/* ------------------------------------------------------------------ */
/*  Global state (same style as fm_app.c)                             */
/* ------------------------------------------------------------------ */
struct globals {
    /* Radio state */
    uint16_t freq_kHz;             /* currently playing               */
    uint16_t sel_freq;             /* VFO selected frequency          */
    bool     is_mr;                /* Memory mode                     */
    uint8_t  sel_ch;               /* selected memory channel         */
    bool     audioEnabled, radioReady;

    /* UI / input */
    uint8_t  savePos, inIdx, stepIndex;
    uint8_t  filteredRssi;
    uint8_t  presetPos;
    int8_t   scanState;            /* 0 = off, +1 up, -1 down         */
    bool     askSave, askDelete, fArm, running, presetMenu, presetError;
    uint16_t fTimer;
    uint8_t inBox[5];
    uint16_t scanTimer, inputTimer;

    const app_api_t *api;
    uint16_t mem[CHMAX];
};

static struct globals g;
#define A (g.api)

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */
static void tune(void);

static bool validPreset(uint8_t c)
{
    return c < CHMAX &&
           g.mem[c] >= SI4732_MIN_FREQUENCY_KHZ &&
           g.mem[c] <= SI4732_MAX_FREQUENCY_KHZ;
}

static void loadPresets(void)
{
    if (A->cfg_load)
        A->cfg_load((uint8_t *)g.mem, sizeof(g.mem));
}

static void savePresets(void)
{
    if (A->cfg_save)
        A->cfg_save((const uint8_t *)g.mem, sizeof(g.mem));
}

static void tuneEnteredFrequency(void)
{
    uint32_t f = 0;
    for (uint8_t i = 0; i < g.inIdx; i++)
        f = f * 10 + g.inBox[i];

    g.inIdx = 0;
    g.inputTimer = 0;
    if (g.presetMenu) {
        if (f >= SI4732_MIN_FREQUENCY_KHZ &&
            f <= SI4732_MAX_FREQUENCY_KHZ) {
            g.mem[g.presetPos] = (uint16_t)f;
            savePresets();
            g.presetError = false;
        } else g.presetError = true;
        return;
    }

    if (f < SI4732_MIN_FREQUENCY_KHZ ||
        f > SI4732_MAX_FREQUENCY_KHZ) return;

    g.is_mr = false;
    g.sel_freq = (uint16_t)f;
    g.freq_kHz = (uint16_t)f;
    tune();
}

static uint16_t wrapFrequency(int32_t frequency)
{
    if (frequency < (int32_t)SI4732_MIN_FREQUENCY_KHZ)
        return SI4732_MAX_FREQUENCY_KHZ;
    if (frequency > (int32_t)SI4732_MAX_FREQUENCY_KHZ)
        return SI4732_MIN_FREQUENCY_KHZ;
    return (uint16_t)frequency;
}

static void tune(void)
{
    SI4732_SetFrequency(g.freq_kHz);
}

static void nextTuneStep(void)
{
    if (++g.stepIndex >= sizeof(tuneSteps) / sizeof(tuneSteps[0]))
        g.stepIndex = 0;
}

static uint8_t findNext(uint8_t c, int8_t dir)
{
    for (uint8_t i = 0; i < CHMAX; i++) {
        if (c == 0xFF) c = CHMAX - 1;
        else if (c >= CHMAX) c = 0;
        if (validPreset(c)) return c;
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
    char *o = A->append_u32(s, f);
    uint8_t len = (uint8_t)(o - s);
    if (len <= 3u) {
        s[5] = '\0';
        s[4] = s[2];
        s[3] = s[1];
        s[2] = s[0];
        s[0] = '0';
        s[1] = '.';
        o = s + 5;
    } else {
        uint8_t dot = len - 3u;
        s[len + 1u] = '\0';
        for (uint8_t i = len; i > dot; i--)
            s[i] = s[i - 1u];
        s[dot] = '.';
        o = s + len + 1u;
    }
    uint8_t x = (uint8_t)(((120u - (uint16_t)(o - s) * 13u) / 2u) + 6u);
    A->display_freq(s, x, 1, false);
}

static void drawAudioWaveform(void)
{
    uint16_t samples[WAVE_SAMPLES];
    int32_t sum = 0;
    uint32_t deadline = A->cycles_48mhz();

    for (uint8_t i = 0; i < WAVE_SAMPLES; i++) {
        while (app_cycles_before(A->cycles_48mhz(), deadline)) {}
        samples[i] = A->af_capture_sample();
        sum += samples[i];
        deadline += WAVE_PERIOD_CYCLES;
    }

    int32_t center = sum / WAVE_SAMPLES;
    int16_t prevX = 32;
    int16_t prevY = 37;

    A->draw_line(A->fb, 32, 37, 93, 37, true);

    for (uint8_t i = 0; i < WAVE_SAMPLES; i++) {
        int32_t amplitude = (int32_t)samples[i] - center;
        int32_t offset = (amplitude * 7) >> 5;
        if (offset > 7) offset = 7;
        if (offset < -7) offset = -7;

        int16_t x = (int16_t)(32 + i * 2);
        int16_t y = (int16_t)(37 - offset);
        if (i > 0)
            A->draw_line(A->fb, prevX, prevY, x, y, true);
        prevX = x;
        prevY = y;
    }
}

static void drawAudioStatus(void)
{
    static const uint8_t speaker[10] = {
        0x1C, 0x1C, 0x3E, 0x7F, 0x00,
        0x22, 0x1C, 0x41, 0x22, 0x1C
    };

    const uint8_t x = 59u;
    for (uint8_t i = 0; i < 10u; i++)
        A->status_line[x + i] = g.audioEnabled ? speaker[i] : 0u;
}

static void drawSignalBars(int16_t rssiDbm)
{
    uint8_t bars = rssiDbm >= -70 ? 4 : rssiDbm >= -80 ? 3 :
                   rssiDbm >= -90 ? 2 : 1;
    for (uint8_t i = 0; i < 4; i++) {
        int16_t x = (int16_t)(2 + i * 5);
        int16_t top = (int16_t)(43 - (i + 1) * 2);
        bool active = i < bars;
        A->draw_rect(A->fb, x, top, (int16_t)(x + 2), 44, true);
        if (active) {
            A->draw_line(A->fb, (int16_t)(x + 1), (int16_t)(top + 1),
                         (int16_t)(x + 1), 43, true);
        }
    }
}

static void draw(void)
{
    char b[16];

    A->display_clear();
    A->status_clear();
    A->draw_battery();
    drawAudioStatus();

    uint8_t titleEnd = g.presetMenu ? 18u : 42u;
    A->print_inverse(g.presetMenu ? "PSET" : "HF-Receive",
                     2, 0, true, true, titleEnd);
    if (g.fArm)
        A->print_tiny("F", g.presetMenu ? 20u : 44u, 1, true, true);
    if (!g.presetMenu) {
        A->print_tiny(tuneStepLabels[g.stepIndex], 0, 19, false, true);
        A->print_tiny("1.3.2 Xserv", 42, 49, false, true);
    }

    if (g.presetMenu) {
        char slot[4];
        slot[0] = 'P';
        slot[1] = g.presetPos == 9 ? '1' :
                  (char)('1' + g.presetPos);
        slot[2] = g.presetPos == 9 ? '0' : '\0';
        slot[3] = '\0';
        A->print_string(slot, 0, 44, 3, 8);
        if (g.presetError)
            A->print_string("RANGE", 80, 127, 3, 8);
    } else if (g.askSave) {
        A->print_string("SAVE?", 0, 127, 3, 8);
    } else if (g.askDelete) {
        A->print_string("DEL?", 0, 127, 3, 8);
    } else if (g.scanState) {
        A->print_string("M-SCAN", 0, 127, 3, 8);
    } else if (g.is_mr) {
        char tmp[8];
        char *p = tmp;
        p = A->append_u32(p, g.sel_ch + 1);
        *p = '\0';
        A->print_string(tmp, 0, 40, 3, 8);
        A->print_string("MR", 50, 127, 3, 8);
    }

    if (g.presetMenu && g.inIdx == 0) {
        if (validPreset(g.presetPos))
            freqBig(g.mem[g.presetPos]);
        else
            A->print_string("EMPTY", 0, 127, 1, 10);
    } else if (g.inIdx > 0) {
        for (uint8_t k = 0; k < 5; k++) {
            b[k] = (k < g.inIdx) ? (char)('0' + g.inBox[k]) : '-';
        }
        b[5] = '\0';
        A->print_string(b, 0, 127, 1, 10);
    } else {
        freqBig(g.freq_kHz);
    }

    if (!g.presetMenu) {
        A->print_tiny("AM", 105, 9, false, true);
        A->print_tiny("MHz", 100, 18, false, true);

        uint8_t rssi = SI4732_GetRSSI();
        g.filteredRssi = (uint8_t)(((uint16_t)g.filteredRssi * 3u +
                                    rssi + 2u) >> 2);
        rssi = g.filteredRssi;
        /* AM RSSI is reported in dBuV; convert to dBm for a 50-ohm input. */
        int16_t rssiDbm = (int16_t)rssi - 107;
        char rssiText[12];
        char *r = A->append_i32(rssiText, rssiDbm);
        *r++ = ' ';
        *r++ = 'd';
        *r++ = 'B';
        *r++ = 'm';
        *r = '\0';
        A->print_tiny(rssiText, 0, 1, false, true);
        drawSignalBars(rssiDbm);
        drawAudioWaveform();
        A->draw_line(A->fb, 0, 47, 127, 47, true);
    }
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
    g.freq_kHz = wrapFrequency((int32_t)g.freq_kHz +
                                g.scanState * tuneSteps[g.stepIndex]);
    tune();
    g.scanTimer = SCAN_SETTLE;
}

static void stopScan(void)
{
    if (!g.is_mr) {
        g.sel_freq = g.freq_kHz;
    }
    g.scanState = 0;
    configChannel();
    tune();
}

static void scanStep(void)
{
    uint8_t rssi = SI4732_GetRSSI();
    if (rssi >= SCAN_RSSI_THRESHOLD) {
        if (SI4732_GetRSSI() < SCAN_RSSI_THRESHOLD) {
            scanTuneNext();
            return;
        }
        g.scanState = 0;
        if (!g.is_mr) g.sel_freq = g.freq_kHz;
        return;
    }
    scanTuneNext();
}

static void startManual(int8_t dir)
{
    if (g.scanState) {
        stopScan();
        return;
    }
    g.scanState = dir;
    scanTuneNext();
}

static void toggleAudio(void)
{
    g.audioEnabled = !g.audioEnabled;
    A->audio_path(g.audioEnabled);
    if (g.radioReady)
        SI4732_SetVolume(g.audioEnabled ? SI4732_MAX_VOLUME : 0);
}

/* ------------------------------------------------------------------ */
/*  Digit entry                                                       */
/* ------------------------------------------------------------------ */
static void digit(uint8_t d)
{
    if (g.askDelete || g.scanState) return;

    if (g.presetMenu) {
        if (g.inIdx < 5) g.inBox[g.inIdx++] = d;
        g.inputTimer = g.inIdx < 5 ? INPUT_TIMEOUT_MS : 0;
        g.presetError = false;
        return;
    }

    if (g.askSave || g.is_mr) {
        /* 2-digit channel */
        g.inputTimer = 0;
        if (g.inIdx < 2) g.inBox[g.inIdx++] = d;
        if (g.inIdx >= 2) {
            uint8_t c = (uint8_t)(g.inBox[0] * 10 + g.inBox[1] - 1);
            g.inIdx = 0;
            if (g.askSave) {
                if (c < CHMAX) g.savePos = c;
            } else if (validPreset(c)) {
                g.sel_ch = c;
                g.freq_kHz = g.mem[c];
                tune();
            }
        }
        return;
    }

    /* Let the user finish 3- or 4-digit frequencies before committing. */
    if (g.inIdx < 5) g.inBox[g.inIdx++] = d;
    g.inputTimer = INPUT_TIMEOUT_MS;
    if (g.inIdx >= 5) tuneEnteredFrequency();
}

/* ------------------------------------------------------------------ */
/*  MENU / UP-DOWN / VFO-MR                                           */
/* ------------------------------------------------------------------ */
static void menu(void)
{
    if (g.presetMenu) {
        if (g.inIdx) {
            tuneEnteredFrequency();
        } else if (validPreset(g.presetPos)) {
            g.freq_kHz = g.mem[g.presetPos];
            g.sel_freq = g.freq_kHz;
            g.is_mr = false;
            g.presetMenu = false;
            tune();
        }
        return;
    }

    g.inIdx = 0;
    g.inputTimer = 0;
    if (!g.is_mr) {
        if (g.askSave) {
            g.mem[g.savePos] = g.freq_kHz;
            savePresets();
        }
        g.askSave = !g.askSave;
    } else {
        if (g.askDelete) {
            g.mem[g.sel_ch] = 0xFFFF;
            savePresets();
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

    if (g.presetMenu) {
        if (step > 0) {
            if (++g.presetPos >= CHMAX) g.presetPos = 0;
        } else if (g.presetPos == 0) {
            g.presetPos = CHMAX - 1;
        } else {
            g.presetPos--;
        }
        g.inIdx = 0;
        g.inputTimer = 0;
        g.presetError = false;
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

    g.freq_kHz = wrapFrequency((int32_t)g.freq_kHz +
                               step * tuneSteps[g.stepIndex]);
    g.sel_freq = g.freq_kHz;
    tune();
}

static void toggleMr(void)
{
    g.inIdx = 0;
    g.inputTimer = 0;
    g.is_mr = !g.is_mr;
    if (configChannel() == 0) tune();
}

static void handleKey(uint8_t key)
{
    if (key == APP_KEY_SIDE1) {
        if (g.scanState) stopScan();
        g.presetMenu = !g.presetMenu;
        g.fArm = false;
        g.fTimer = 0;
        g.askSave = false;
        g.askDelete = false;
        g.inIdx = 0;
        g.inputTimer = 0;
        g.presetError = false;
        return;
    }

    if (g.presetMenu && key == APP_KEY_EXIT) {
        g.presetMenu = false;
        g.fArm = false;
        g.fTimer = 0;
        g.inIdx = 0;
        g.inputTimer = 0;
        return;
    }

    if (key == APP_KEY_SIDE2) {
        g.fArm = false;
        g.fTimer = 0;
        nextTuneStep();
        return;
    }

    if (key == APP_KEY_F) {
        if (g.fTimer) {
            g.fArm = false;
            g.fTimer = 0;
            startManual(+1);
            return;
        }
        g.fArm = true;
        g.fTimer = F_KEY_TIMEOUT_MS;
        return;
    }

    bool fn = g.fArm;
    g.fArm = false;
    g.fTimer = 0;

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
        case APP_KEY_3:
            if (fn) toggleMr();
            break;
        case APP_KEY_STAR:
            if (!fn) toggleAudio();
            break;
        case APP_KEY_EXIT:
            if (g.scanState) stopScan();
            else if (g.inIdx || g.askSave || g.askDelete) {
                g.inIdx = 0;
                g.inputTimer = 0;
                g.askSave = false;
                g.askDelete = false;
            } else {
                g.running = false;
            }
            break;
        default:
            break;
    }
}

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    if (!api) return;

    g.api = api;
    g.stepIndex = (uint8_t)(sizeof(tuneSteps) / sizeof(tuneSteps[0]) - 1);
    g.freq_kHz = SI4732_MIN_FREQUENCY_KHZ;
    g.sel_freq = g.freq_kHz;
    g.running = true;
    g.audioEnabled = true;
    loadPresets();

    A->led(false);
    A->backlight_on();

    g.radioReady = SI4732_Init();
    if (!g.radioReady) {
        A->display_clear();
        A->status_clear();
        A->print_string("SI4732 ERROR", 0, 127, 2, 8);
        A->blit_status();
        A->blit_full();
    } else {
        tune();
        A->audio_path(g.audioEnabled);
        A->af_capture_open();
        show();
    }

    uint8_t lastKey = APP_KEY_INVALID;
    uint16_t batteryTimer = 100;
    uint16_t waveformTimer = 80;
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

        if (g.inputTimer) {
            if (g.inputTimer <= 10) {
                if (g.presetMenu)
                    g.inputTimer = 0;
                else
                    tuneEnteredFrequency();
                redraw = true;
            } else {
                g.inputTimer -= 10;
            }
        }

        if (g.fTimer) {
            if (g.fTimer <= 10) {
                g.fTimer = 0;
            } else {
                g.fTimer -= 10;
            }
        }

        if (batteryTimer <= 10) {
            batteryTimer = 100;
            A->battery_sample();
            redraw = true;
        } else {
            batteryTimer -= 10;
        }

        if (g.radioReady) {
            if (waveformTimer <= 10) {
                waveformTimer = 80;
                redraw = true;
            } else {
                waveformTimer -= 10;
            }
        }

        if (redraw) show();
        A->backlight_update();
        A->delay_ms(10);
    }

    if (g.radioReady) {
        A->af_capture_close();
        A->audio_path(false);
        SI4732_PowerDown();
    }
    A->led(false);
}

