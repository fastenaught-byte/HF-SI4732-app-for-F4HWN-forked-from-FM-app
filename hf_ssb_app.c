#include <stdint.h>
#include <stdbool.h>
#include "app_api.h"
#include "si4732_minimal.h"

#define INPUT_TIMEOUT_MS 1000u
#define F_KEY_TIMEOUT_MS 300u

static const uint8_t steps[] = {12, 10, 9, 5, 2, 1};
static const char stepLabels[][4] = {"12k", "10k", "9k", "5k", "2k", "1k"};

static struct {
    const app_api_t *api;
    uint16_t frequency;
    uint16_t inputTimer;
    uint16_t fTimer;
    uint8_t stepIndex;
    uint8_t input[5];
    uint8_t inputLength;
    si_mode_t mode;
    bool audioEnabled;
    bool radioReady;
    bool patchError;
    bool radioError;
    bool running;
} g;

#define A (g.api)

static void drawAudioIcon(void)
{
    A->draw_rect(A->fb, 2, 13, 3, 15, true);
    A->draw_line(A->fb, 3, 13, 5, 11, true);
    A->draw_line(A->fb, 3, 15, 5, 17, true);
    A->draw_line(A->fb, 5, 11, 5, 17, true);
    if (g.audioEnabled) {
        A->draw_line(A->fb, 6, 12, 7, 14, true);
        A->draw_line(A->fb, 7, 14, 6, 16, true);
        A->draw_line(A->fb, 7, 11, 8, 14, true);
        A->draw_line(A->fb, 8, 14, 7, 17, true);
    } else {
        A->draw_line(A->fb, 6, 12, 8, 16, true);
        A->draw_line(A->fb, 8, 12, 6, 16, true);
    }
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
    if (!g.radioReady) return;
    if (!SI4732_SetFrequency(g.frequency))
        g.radioError = true;
    else
        g.radioError = false;
}

static void draw(void)
{
    char text[12];
    char *end;

    A->display_clear();
    A->status_clear();
    A->draw_battery();
    A->print_inverse("HF", 2, 0, false, true, 12);
    A->print_tiny(stepLabels[g.stepIndex], 15, 0, false, true);
    drawAudioIcon();
    A->print_tiny(g.mode == SI_MODE_USB ? "USB" :
                  g.mode == SI_MODE_LSB ? "LSB" : "AM", 112, 0, false, true);

    if (g.inputLength) {
        for (uint8_t i = 0; i < 5; i++)
            text[i] = i < g.inputLength ? (char)('0' + g.input[i]) : '-';
        text[5] = '\0';
        A->print_string(text, 0, 127, 1, 10);
    } else {
        end = A->append_u32(text, g.frequency);
        uint8_t length = (uint8_t)(end - text);
        if (length <= 3u) {
            text[5] = '\0';
            text[4] = text[2];
            text[3] = text[1];
            text[2] = text[0];
            text[0] = '0';
            text[1] = '.';
            end = text + 5;
        } else {
            uint8_t dot = length - 3u;
            text[length + 1u] = '\0';
            for (uint8_t i = length; i > dot; i--)
                text[i] = text[i - 1u];
            text[dot] = '.';
            end = text + length + 1u;
        }
        uint8_t x = (uint8_t)((128u - (uint16_t)(end - text) * 13u) / 2u);
        A->display_freq(text, x, 1, false);
    }

    A->print_tiny("MHz", 96, 18, false, true);
    if (g.radioReady) {
        uint8_t rssi = SI4732_GetRSSI();
        if (rssi == 0) {
            A->print_tiny("NO SIG", 0, 56, false, true);
        } else {
            int16_t dbm = (int16_t)rssi - 107;
            end = A->append_i32(text, dbm);
            *end++ = ' ';
            *end++ = 'd';
            *end++ = 'B';
            *end++ = 'm';
            *end = '\0';
            A->print_tiny(text, 0, 56, false, true);
        }
    } else {
        A->print_tiny("NO RADIO", 0, 56, false, true);
    }

    if (g.radioError)
        A->print_string("SI4732 ERROR", 0, 127, 3, 8);
    else if (g.patchError)
        A->print_string("SSB PATCH ERROR", 0, 127, 3, 8);
}

static void show(void)
{
    draw();
    A->blit_status();
    A->blit_full();
}

static void toggleAudio(void)
{
    g.audioEnabled = !g.audioEnabled;
    A->audio_path(g.audioEnabled);
    if (g.radioReady)
        SI4732_SetVolume(g.audioEnabled ? SI4732_MAX_VOLUME : 0);
}

static void changeMode(void)
{
    if (!g.radioReady) return;
    g.patchError = false;
    si_mode_t nextMode;
    if (g.mode == SI_MODE_AM) {
        if (!SI4732_LoadSsbPatch(A)) {
            SI4732_PowerDown();
            A->delay_ms(10);
            g.radioReady = SI4732_Init();
            g.radioError = !g.radioReady;
            if (g.radioReady) {
                tune();
                SI4732_SetVolume(g.audioEnabled ? SI4732_MAX_VOLUME : 0);
            }
            g.patchError = true;
            return;
        }
        nextMode = SI_MODE_USB;
    } else if (g.mode == SI_MODE_USB) {
        nextMode = SI_MODE_LSB;
    } else {
        nextMode = SI_MODE_AM;
    }

    if (!SI4732_SetMode(nextMode)) {
        g.radioError = true;
        return;
    }
    g.mode = nextMode;
    tune();
    SI4732_SetVolume(g.audioEnabled ? SI4732_MAX_VOLUME : 0);
}

static void commitFrequency(void)
{
    uint32_t frequency = 0;
    for (uint8_t i = 0; i < g.inputLength; i++)
        frequency = frequency * 10u + g.input[i];
    g.inputLength = 0;
    g.inputTimer = 0;
    if (frequency < SI4732_MIN_FREQUENCY_KHZ ||
        frequency > SI4732_MAX_FREQUENCY_KHZ)
        return;
    g.frequency = (uint16_t)frequency;
    tune();
}

static void digit(uint8_t value)
{
    if (g.inputLength < sizeof(g.input))
        g.input[g.inputLength++] = value;
    g.inputTimer = INPUT_TIMEOUT_MS;
    if (g.inputLength == sizeof(g.input))
        commitFrequency();
}

static void handleKey(uint8_t key)
{
    if (key == APP_KEY_SIDE2) {
        g.fTimer = 0;
        if (++g.stepIndex >= sizeof(steps) / sizeof(steps[0]))
            g.stepIndex = 0;
        return;
    }

    if (key == APP_KEY_F) {
        g.fTimer = F_KEY_TIMEOUT_MS;
        return;
    }

    bool function = g.fTimer != 0;
    g.fTimer = 0;
    if (!function && key <= APP_KEY_9) {
        digit(key);
        return;
    }

    switch (key) {
    case APP_KEY_UP:
        g.frequency = wrapFrequency((int32_t)g.frequency + steps[g.stepIndex]);
        g.inputLength = 0;
        g.inputTimer = 0;
        tune();
        break;
    case APP_KEY_DOWN:
        g.frequency = wrapFrequency((int32_t)g.frequency - steps[g.stepIndex]);
        g.inputLength = 0;
        g.inputTimer = 0;
        tune();
        break;
    case APP_KEY_2:
        if (function) changeMode();
        break;
    case APP_KEY_STAR:
        toggleAudio();
        break;
    case APP_KEY_EXIT:
        g.running = false;
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
    g.frequency = 7000;
    g.stepIndex = (uint8_t)(sizeof(steps) / sizeof(steps[0]) - 1u);
    g.mode = SI_MODE_AM;
    g.audioEnabled = true;
    g.running = true;
    g.patchError = false;

    A->led(false);
    A->backlight_on();
    g.radioReady = SI4732_Init();
    g.radioError = !g.radioReady;
    if (g.radioReady) {
        tune();
        A->audio_path(true);
    }
    show();

    uint8_t lastKey = APP_KEY_INVALID;
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

        if (g.fTimer) {
            if (g.fTimer <= 10u) {
                g.fTimer = 0;
            } else {
                g.fTimer -= 10u;
            }
        }
        if (g.inputTimer) {
            if (g.inputTimer <= 10u) {
                commitFrequency();
                redraw = true;
            } else {
                g.inputTimer -= 10u;
            }
        }
        if (redraw) show();
        A->backlight_update();
        A->delay_ms(10);
    }

    if (g.radioReady) {
        A->audio_path(false);
        SI4732_PowerDown();
    }
    A->led(false);
}
