#include "si4732_minimal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t count)
{
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (count--) *d++ = *s++;
    return dest;
}

void *memset(void *dest, int value, size_t count)
{
    uint8_t *d = dest;
    while (count--) *d++ = (uint8_t)value;
    return dest;
}

/* ------------------------------------------------------------------ */
/* Direct GPIO access for PY32F071                                   */
/* ------------------------------------------------------------------ */
#define GPIOA_BASE       0x50000000u
#define GPIOA_MODER      (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPER     (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_PUPDR      (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_IDR        (*(volatile uint32_t *)(GPIOA_BASE + 0x10))
#define GPIOA_BSRR       (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

#define SCL_BIT          (1u << I2C_SCL_PIN)
#define SDA_BIT          (1u << I2C_SDA_PIN)
#define I2C_PIN_MASK     (SCL_BIT | SDA_BIT)

static inline void pin_input(uint8_t pin)
{
    GPIOA_MODER &= ~(3u << (pin * 2));
}

static inline void pin_output(uint8_t pin)
{
    uint32_t shift = pin * 2;
    GPIOA_MODER = (GPIOA_MODER & ~(3u << shift)) | (1u << shift);
}

/* very short delay – adjust if needed */
static inline void i2c_delay(void)
{
    for (volatile int i = 0; i < 8; i++) {
        __asm volatile("nop");
    }
}

/* Pin helpers */
static inline void scl_high(void) { pin_input(I2C_SCL_PIN); }
static inline void scl_low(void)  { GPIOA_BSRR = SCL_BIT << 16; pin_output(I2C_SCL_PIN); }
static inline void sda_high(void) { pin_input(I2C_SDA_PIN); }
static inline void sda_low(void)  { GPIOA_BSRR = SDA_BIT << 16; pin_output(I2C_SDA_PIN); }
static inline int  sda_read(void) { return (GPIOA_IDR & SDA_BIT) ? 1 : 0; }

/* ------------------------------------------------------------------ */
/*  Bit-bang I2C                                                      */
/* ------------------------------------------------------------------ */
static void i2c_start(void)
{
    sda_high();
    scl_high();
    i2c_delay();
    sda_low();
    i2c_delay();
    scl_low();
    i2c_delay();
}

static void i2c_stop(void)
{
    sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    sda_high();
    i2c_delay();
}

static void i2c_write_bit(int bit)
{
    if (bit) sda_high();
    else     sda_low();
    i2c_delay();
    scl_high();
    i2c_delay();
    scl_low();
    i2c_delay();
}

static int i2c_read_bit(void)
{
    int bit;
    sda_high();               /* release SDA */
    i2c_delay();
    scl_high();
    i2c_delay();
    bit = sda_read();
    scl_low();
    i2c_delay();
    return bit;
}

static int i2c_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        i2c_write_bit(byte & 0x80);
        byte <<= 1;
    }
    return i2c_read_bit();    /* 0 = ACK */
}

static uint8_t i2c_read_byte(int ack)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte = (byte << 1) | i2c_read_bit();
    }
    i2c_write_bit(!ack);      /* ACK = 0, NACK = 1 */
    return byte;
}

/* ------------------------------------------------------------------ */
/*  SI4732 communication                                              */
/* ------------------------------------------------------------------ */
static bool wait_cts(void)
{
    for (int i = 0; i < 100; i++) {
        i2c_start();
        i2c_write_byte((SI4732_ADDR << 1) | 0);   /* write */
        i2c_write_byte(CMD_GET_INT_STATUS);
        i2c_stop();

        /* small delay */
        for (volatile int d = 0; d < 200; d++) __asm volatile("nop");

        i2c_start();
        i2c_write_byte((SI4732_ADDR << 1) | 1);   /* read */
        uint8_t status = i2c_read_byte(0);        /* NACK */
        i2c_stop();

        if (status & 0x80) return true;           /* CTS */
        /* delay ~1 ms */
        for (volatile int d = 0; d < 3000; d++) __asm volatile("nop");
    }
    return false;
}

static bool send_cmd(const uint8_t *cmd, uint8_t len)
{
    if (!wait_cts()) return false;

    i2c_start();
    if (i2c_write_byte((SI4732_ADDR << 1) | 0)) {  /* NACK? */
        i2c_stop();
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        i2c_write_byte(cmd[i]);
    }
    i2c_stop();
    return true;
}

static bool read_response(uint8_t *buf, uint8_t len)
{
    if (!wait_cts()) return false;

    i2c_start();
    if (i2c_write_byte((SI4732_ADDR << 1) | 1)) {
        i2c_stop();
        return false;
    }
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = i2c_read_byte(i < len - 1);      /* ACK until last */
    }
    i2c_stop();
    return true;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
void SI4732_I2C_Init(void)
{
    uint32_t pullMask = (3u << (I2C_SCL_PIN * 2)) |
                        (3u << (I2C_SDA_PIN * 2));
    uint32_t pullUp = (1u << (I2C_SCL_PIN * 2)) |
                      (1u << (I2C_SDA_PIN * 2));

    GPIOA_BSRR = I2C_PIN_MASK << 16;
    GPIOA_OTYPER |= I2C_PIN_MASK;
    GPIOA_PUPDR = (GPIOA_PUPDR & ~pullMask) | pullUp;
    scl_high();
    sda_high();
}

bool SI4732_Init(void)
{
    SI4732_I2C_Init();

    /* POWER_UP for AM + Analog Audio + Crystal */
    uint8_t cmd[] = { CMD_POWER_UP, 0x11, 0x05 };
    if (!send_cmd(cmd, 3)) return false;

    /* Wait after power-up */
    for (volatile int i = 0; i < 50000; i++) __asm volatile("nop");

    SI4732_SetVolume(45);
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

    uint8_t cmd[5] = {
        CMD_AM_TUNE_FREQ,
        0x00,                         /* AUTO, no FREEZE */
        (uint8_t)(kHz >> 8),
        (uint8_t)(kHz & 0xFF),
        0x00                          /* antenna cap auto */
    };

    if (!send_cmd(cmd, 5)) return false;

    /* settle */
    for (volatile int i = 0; i < 30000; i++) __asm volatile("nop");
    return true;
}

void SI4732_SetVolume(uint8_t vol)
{
    if (vol > 63) vol = 63;
    uint8_t cmd[] = {
        CMD_SET_PROPERTY,
        0x00,
        (uint8_t)(PROP_RX_VOLUME >> 8),
        (uint8_t)(PROP_RX_VOLUME & 0xFF),
        0x00,
        vol
    };
    send_cmd(cmd, 6);
}

uint8_t SI4732_GetRSSI(void)
{
    uint8_t cmd[] = { CMD_AM_RSQ_STATUS, 0x00 };
    if (!send_cmd(cmd, 2)) return 0;

    uint8_t resp[6] = {0};
    if (!read_response(resp, 6)) return 0;

    return resp[2];                   /* RSSI */
}

uint8_t SI4732_GetSNR(void)
{
    uint8_t cmd[] = { CMD_AM_RSQ_STATUS, 0x00 };
    if (!send_cmd(cmd, 2)) return 0;

    uint8_t resp[6] = {0};
    if (!read_response(resp, 6)) return 0;

    return resp[3];                   /* SNR */
}

bool SI4732_SetMode(si_mode_t mode)
{
    if (mode == SI_MODE_AM) return true;
    return false;                     /* USB/LSB needs patch */
}

void SI4732_SetBFO(int16_t bfo_hz)
{
    (void)bfo_hz;                     /* not available without patch */
}

/* Band table */
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
const uint8_t si_band_count = sizeof(si_bands) / sizeof(si_bands[0]);
