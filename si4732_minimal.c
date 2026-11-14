#include "si4732_minimal.h"
/* In real overlay you need delay + (bit-bang) I2C.
   For now assume you have:
   - void delay_ms(uint32_t);
   - soft I2C: i2c_start(), i2c_write(uint8_t), i2c_read(), i2c_stop()
   Pins: PA10 = SCL, PA11 = SDA (same as original BK1080)
*/

/* ---------- tiny Soft-I2C skeleton (you must implement with GPIO) ---------- */
/* static void i2c_start(void); ... etc. – leave as TODO or copy from CEC */

static bool wait_cts(void)
{
    /* poll GET_INT_STATUS until CTS bit set (max ~50 ms) */
    for (int i = 0; i < 50; i++) {
        /* send CMD_GET_INT_STATUS, read 1 byte, check bit 7 */
        delay_ms(1);
        /* if (status & 0x80) return true; */
    }
    return false;
}

static bool send_cmd(const uint8_t *cmd, uint8_t len)
{
    if (!wait_cts()) return false;
    /* i2c_start(); i2c_write(SI4732_ADDR << 1); ... write bytes; i2c_stop(); */
    return true;   /* placeholder */
}

bool SI4732_Init(void)
{
    /* POWER_UP for AM (ARG1 = 0x11 = AM + analog audio, crystal) */
    uint8_t cmd[] = { CMD_POWER_UP, 0x11, 0x05 };  /* classic values from PU2CLR/CEC */
    if (!send_cmd(cmd, 3)) return false;
    delay_ms(15);   /* maxDelayAfterPowerUp */

    /* optional: set volume, channel filter etc. */
    SI4732_SetVolume(48);
    return true;
}

void SI4732_PowerDown(void)
{
    uint8_t cmd[] = { CMD_POWER_DOWN };
    send_cmd(cmd, 1);
}

bool SI4732_SetFrequency(uint16_t kHz)
{
    if (kHz < 150 || kHz > 30000) return false;

    uint8_t cmd[5];
    cmd[0] = CMD_AM_TUNE_FREQ;
    cmd[1] = 0x00;                    /* no FREEZE, AUTO */
    cmd[2] = (kHz >> 8) & 0xFF;
    cmd[3] = kHz & 0xFF;
    cmd[4] = 0x00;                    /* antenna cap = auto */

    if (!send_cmd(cmd, 5)) return false;
    delay_ms(30);                     /* tune settle */
    return true;
}

void SI4732_SetVolume(uint8_t vol)
{
    if (vol > 63) vol = 63;
    uint8_t cmd[] = {
        CMD_SET_PROPERTY,
        0x00,
        (PROP_RX_VOLUME >> 8), (PROP_RX_VOLUME & 0xFF),
        0x00, vol
    };
    send_cmd(cmd, 6);
}

uint8_t SI4732_GetRSSI(void)
{
    /* AM_RSQ_STATUS → RESP2 contains RSSI */
    /* send CMD_AM_RSQ_STATUS, read response, return RESP2 */
    return 0;   /* placeholder */
}

uint8_t SI4732_GetSNR(void)
{
    /* same RSQ status, RESP3 = SNR */
    return 0;
}

bool SI4732_SetMode(si_mode_t mode)
{
    if (mode == SI_MODE_AM) {
        /* already in AM after Init */
        return true;
    }
    /* USB/LSB need SSB patch → not available in pure overlay yet */
    return false;
}

void SI4732_SetBFO(int16_t bfo_hz)
{
    /* only valid after SSB patch loaded */
    (void)bfo_hz;
}

/* Example bands – can live in flash/assets */
const si_band_t si_bands[] = {
    { "LW",   150,   520,  9 },
    { "MW",   520,  1710,  9 },
    { "160M", 1800, 2000,  1 },
    { "80M",  3500, 4000,  1 },
    { "40M",  7000, 7300,  1 },
    { "20M", 14000,14350,  1 },
    { "15M", 21000,21450,  1 },
    { "10M", 28000,29700,  1 },
};
const uint8_t si_band_count = sizeof(si_bands)/sizeof(si_bands[0]);
