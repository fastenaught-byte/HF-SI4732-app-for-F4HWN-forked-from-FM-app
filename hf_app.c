#include "app_api.h"          /* the real one from F4HWN */
#include "si4732_minimal.h"

/* simple state */
static uint16_t g_freq_kHz = 7200;   /* start on 40 m */
static uint8_t  g_band_idx = 4;
static si_mode_t g_mode = SI_MODE_AM;

void app_main(const app_api_t *A)
{
    /* --- init --- */
    A->display_clear();
    A->status_clear();

    if (!SI4732_Init()) {
        A->print_bold("SI4732 FAIL", 0, 127, 3);
        A->blit_full();
        while (1) { if (A->get_key() == APP_KEY_EXIT) return; }
    }

    SI4732_SetFrequency(g_freq_kHz);

    /* --- main loop --- */
    for (;;) {
        uint8_t key = A->get_key();

        if (key == APP_KEY_EXIT) {
            SI4732_PowerDown();
            return;
        }

        /* UP / DOWN = tune */
        int8_t dir = A->nav_dir(key);
        if (dir) {
            g_freq_kHz += dir * si_bands[g_band_idx].step_kHz;
            if (g_freq_kHz < si_bands[g_band_idx].lo_kHz)
                g_freq_kHz = si_bands[g_band_idx].lo_kHz;
            if (g_freq_kHz > si_bands[g_band_idx].hi_kHz)
                g_freq_kHz = si_bands[g_band_idx].hi_kHz;
            SI4732_SetFrequency(g_freq_kHz);
        }

        /* F / MENU = next band (example) */
        if (key == APP_KEY_F || key == APP_KEY_MENU) {
            g_band_idx = (g_band_idx + 1) % si_band_count;
            g_freq_kHz = si_bands[g_band_idx].lo_kHz;
            SI4732_SetFrequency(g_freq_kHz);
        }

        /* draw */
        A->display_clear();
        char buf[16];
        /* simple frequency print – use A->display_freq or print_normal */
        A->print_bold(si_bands[g_band_idx].name, 0, 40, 0);
        /* ... format g_freq_kHz into buf ... */
        A->display_freq(buf, 0, 2, false);

        uint8_t rssi = SI4732_GetRSSI();
        /* draw RSSI bar or number */
        A->blit_full();

        A->delay_ms(50);   /* simple rate limit */
    }
}
