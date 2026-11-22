#ifndef SI4732_MINIMAL_H
#define SI4732_MINIMAL_H

#include <stdint.h>
#include <stdbool.h>

/* SI4732 I2C address (SEN = LOW) */
#define SI4732_ADDR          0x11

/* AM tuning limits from the SI4732 datasheet, in kHz. */
#define SI4732_MIN_FREQUENCY_KHZ 150u
#define SI4732_MAX_FREQUENCY_KHZ 30000u
#define SI4732_MAX_VOLUME        63u

/* Commands we actually need */
#define CMD_POWER_UP         0x01
#define CMD_POWER_DOWN       0x11
#define CMD_SET_PROPERTY     0x12
#define CMD_GET_INT_STATUS   0x14
#define CMD_AM_TUNE_FREQ     0x40
#define CMD_AM_TUNE_STATUS   0x42
#define CMD_AM_RSQ_STATUS    0x43

/* Soft-I2C Pins (BK1080 original pins) */
#define I2C_SCL_PORT     0x50000000u   /* GPIOA base */
#define I2C_SDA_PORT     0x50000000u   /* GPIOA base */
#define I2C_SCL_PIN      10            /* PA10 */
#define I2C_SDA_PIN      11            /* PA11 */

/* Public low-level if needed */
void SI4732_I2C_Init(void);

/* Useful properties */
#define PROP_RX_VOLUME       0x4000
#define PROP_AM_AVC_MAX_GAIN 0x3103
#define PROP_AM_CHANNEL_FILTER 0x3102
#define PROP_AM_SOFT_MUTE_MAX_ATTENUATION 0x3302
#define AM_AVC_MAX_GAIN      0x7800
#define PROP_SSB_BFO         0x0100
#define PROP_SSB_MODE        0x0101

/* Modes */
typedef enum {
    SI_MODE_AM = 0,
    SI_MODE_USB,          /* needs SSB patch */
    SI_MODE_LSB
} si_mode_t;

/* Public API – only what we need */
bool     SI4732_Init(void);                    /* Power-Up in AM */
void     SI4732_PowerDown(void);
bool     SI4732_SetFrequency(uint16_t kHz);
void     SI4732_SetVolume(uint8_t vol);        /* 0-63 */
uint8_t  SI4732_GetRSSI(void);                 /* 0-127 */
uint8_t  SI4732_GetSNR(void);
bool     SI4732_SetMode(si_mode_t mode);
void     SI4732_SetBFO(int16_t bfo_hz);
struct app_api;
bool     SI4732_LoadSsbPatch(const struct app_api *api);

/* Band helpers (example) */
typedef struct {
    const char *name;
    uint16_t    lo_kHz;
    uint16_t    hi_kHz;
    uint16_t    step_kHz;
} si_band_t;

extern const si_band_t si_bands[];
extern const uint8_t   si_band_count;

#endif
