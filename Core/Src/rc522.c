/* MFRC522 register interface: NXP MFRC522 data sheet, SPI mode 0.
   Single-card ISO14443A UID acquisition; collisions are rejected. */
#include "rc522.h"
#include "spi.h"
#include "esp.h"
#include <string.h>

enum {
    REG_COMMAND = 0x01, REG_IRQ = 0x04, REG_ERROR = 0x06,
    REG_STATUS2 = 0x08, REG_FIFO = 0x09, REG_FIFO_LEVEL = 0x0A,
    REG_CONTROL = 0x0C, REG_FRAMING = 0x0D, REG_COLL = 0x0E,
    REG_MODE = 0x11, REG_TX_MODE = 0x12, REG_RX_MODE = 0x13,
    REG_TX_CONTROL = 0x14, REG_TX_ASK = 0x15, REG_RX_GAIN = 0x26,
    REG_TMODE = 0x2A, REG_TPRESCALER = 0x2B,
    REG_TRELOAD_H = 0x2C, REG_TRELOAD_L = 0x2D, REG_VERSION = 0x37
};

static rc522_diagnostic_t diagnostic;

const rc522_diagnostic_t *rc522_diagnostic(void)
{
    return &diagnostic;
}

static int write_reg(uint8_t address, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)((address << 1U) & 0x7EU), value};
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_Transmit(&hspi2, tx, sizeof(tx), 10);
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
    return result == HAL_OK ? 0 : -1;
}

static int read_reg(uint8_t address, uint8_t *value)
{
    uint8_t tx[2] = {(uint8_t)(((address << 1U) & 0x7EU) | 0x80U), 0};
    uint8_t rx[2];
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(&hspi2, tx, rx, sizeof(tx), 10);
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
    if (result != HAL_OK) return -1;
    *value = rx[1];
    return 0;
}

static void crc_a(const uint8_t *data, uint8_t length, uint8_t *result)
{
    uint16_t crc = 0x6363U;
    for (uint8_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) ? 0x8408U : 0U));
    }
    result[0] = (uint8_t)crc;
    result[1] = (uint8_t)(crc >> 8U);
}

/* 0: response, 1: RF timeout, -1: error. Capacity is in/out. */
static int exchange(const uint8_t *tx, uint8_t tx_length, uint8_t tx_bits,
                    uint8_t *rx, uint8_t *rx_length, uint8_t *rx_bits)
{
    uint8_t irq, error, count, control;
    diagnostic.irq = diagnostic.error = diagnostic.fifo_count = diagnostic.last_bits = 0;
    diagnostic.reason = "SPI transfer failed";
    if (write_reg(REG_COMMAND, 0) || write_reg(REG_IRQ, 0x7F) ||
        write_reg(REG_FIFO_LEVEL, 0x80) || write_reg(REG_FRAMING, tx_bits)) return -1;
    for (uint8_t i = 0; i < tx_length; ++i)
        if (write_reg(REG_FIFO, tx[i])) return -1;
    if (write_reg(REG_COMMAND, 0x0C) || write_reg(REG_FRAMING, (uint8_t)(0x80U | tx_bits)))
        return -1;
    uint32_t start = HAL_GetTick();
    int result = -1;
    diagnostic.reason = "RF completion timeout";
    do {
        /* Keep ESP UART bytes drained during RF waits; no application callbacks. */
        esp_poll();
        if (read_reg(REG_IRQ, &irq)) {
            diagnostic.reason = "SPI IRQ read failed";
            break;
        }
        diagnostic.irq = irq;
        if (irq & 0x22U) {
            if (read_reg(REG_ERROR, &error)) {
                diagnostic.reason = "SPI error register read failed";
                break;
            }
            diagnostic.error = error;
            if (error & 0xDBU) {
                diagnostic.reason = (error & 0x08U) ? "card collision" : "RF hardware/protocol error";
                break;
            }
        }
        if ((irq & 0x01U) && !(irq & 0x20U)) {
            result = 1;
            break;
        }
        /* IdleIRq is command completion, not proof of received data.
           Only RxIRq permits reading an RF response from FIFO. */
        if (irq & 0x20U) {
            if (read_reg(REG_FIFO_LEVEL, &count) || read_reg(REG_CONTROL, &control)) {
                diagnostic.reason = "SPI FIFO status read failed";
                break;
            }
            diagnostic.fifo_count = count;
            diagnostic.last_bits = control & 0x07U;
            if (count == 0) {
                /* No usable frame: clear this RX event and await the RF timer.
                   Some boards report RxIRq for short/noisy input streams. */
                if (write_reg(REG_IRQ, 0x20)) {
                    diagnostic.reason = "SPI empty RX clear failed";
                    break;
                }
                if (irq & 0x01U) {
                    result = 1;
                    break;
                }
                HAL_Delay(1);
                continue;
            }
            if (count > *rx_length) {
                diagnostic.reason = "unexpected FIFO length";
                break;
            }
            uint8_t i;
            for (i = 0; i < count; ++i)
                if (read_reg(REG_FIFO, rx + i)) break;
            if (i != count) {
                diagnostic.reason = "SPI FIFO data read failed";
                break;
            }
            *rx_length = count;
            *rx_bits = control & 0x07U;
            result = 0;
            diagnostic.reason = "unexpected card response";
            break;
        }
        HAL_Delay(1);
    } while ((uint32_t)(HAL_GetTick() - start) < 40U);
    if (write_reg(REG_FRAMING, tx_bits) || write_reg(REG_COMMAND, 0)) {
        diagnostic.reason = "SPI command cleanup failed";
        return -1;
    }
    return result;
}

int rc522_init(uint8_t *version)
{
    if (version == NULL) return -1;
    *version = 0;
    HAL_GPIO_WritePin(RC522_CS_PORT, RC522_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(RC522_RST_PORT, RC522_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(50);
    if (read_reg(REG_VERSION, version) || *version == 0 || *version == 0xFF) return -1;
    /* Timer: 13.56 MHz/(2*169+1) = 40 kHz; 1000 counts = 25 ms. */
    if (write_reg(REG_COMMAND, 0) || write_reg(REG_TMODE, 0x80) ||
        write_reg(REG_TPRESCALER, 0xA9) || write_reg(REG_TRELOAD_H, 0x03) ||
        write_reg(REG_TRELOAD_L, 0xE8) || write_reg(REG_MODE, 0x3D) ||
        /* RxNoErr ignores receive streams shorter than four bits. */
        write_reg(REG_TX_MODE, 0) || write_reg(REG_RX_MODE, 0x08) ||
        write_reg(REG_TX_ASK, 0x40) || write_reg(REG_RX_GAIN, 0x40) ||
        write_reg(REG_STATUS2, 0)) return -1;
    /* Preserve antenna polarity bits (notably InvTx2RFOn, reset value 0x80).
       Writing 0x03 alone drives both antenna outputs with the same polarity. */
    uint8_t antenna;
    if (read_reg(REG_TX_CONTROL, &antenna) ||
        write_reg(REG_TX_CONTROL, (uint8_t)(antenna | 0x03U))) return -1;
    HAL_Delay(5);
    return 0;
}

static int read_uid_internal(rc522_uid_t *uid)
{
    uint8_t response[5], count = sizeof(response), bits;
    uint8_t wake = 0x52; /* WUPA also wakes a previously halted card. */
    diagnostic.stage = "WUPA";
    if (uid == NULL) return -1;
    memset(uid, 0, sizeof(*uid));
    int result = exchange(&wake, 1, 7, response, &count, &bits);
    if (result == 1) return 0;
    if (result != 0 || count != 2 || bits != 0) return -1;
    for (uint8_t level = 0; level < 3; ++level) {
        diagnostic.stage = "ANTICOLL";
        uint8_t select[9] = {(uint8_t)(0x93U + 2U * level), 0x20};
        uint8_t collision;
        if (read_reg(REG_COLL, &collision) ||
            write_reg(REG_COLL, (uint8_t)(collision & 0x7FU))) return -1;
        count = sizeof(response);
        if (exchange(select, 2, 0, response, &count, &bits) != 0 ||
            count != 5 || bits != 0) return -1;
        if ((response[0] ^ response[1] ^ response[2] ^ response[3]) != response[4]) {
            diagnostic.reason = "UID BCC mismatch";
            return -1;
        }
        uint8_t cascade = response[0] == 0x88;
        if (cascade && level == 2) return -1;
        uint8_t copy_length = cascade ? 3 : 4;
        memcpy(uid->bytes + uid->length, response + (cascade ? 1 : 0), copy_length);
        uid->length += copy_length;
        select[1] = 0x70;
        memcpy(select + 2, response, 5);
        crc_a(select, 7, select + 7);
        count = sizeof(response);
        diagnostic.stage = "SELECT";
        if (exchange(select, sizeof(select), 0, response, &count, &bits) != 0 ||
            count != 3 || bits != 0) return -1;
        uint8_t crc[2];
        crc_a(response, 1, crc);
        if (memcmp(response + 1, crc, 2) != 0) {
            diagnostic.reason = "SAK CRC mismatch";
            return -1;
        }
        if ((response[0] & 0x04U) != 0) {
            if (!cascade) return -1;
            continue;
        }
        if (cascade) return -1;
        /* HLTA has no reply on success. Halting allows WUPA on the next poll. */
        uint8_t halt[4] = {0x50, 0};
        crc_a(halt, 2, halt + 2);
        count = sizeof(response);
        diagnostic.stage = "HALT";
        if (exchange(halt, sizeof(halt), 0, response, &count, &bits) != 1) return -1;
        return 1;
    }
    return -1;
}

int rc522_read_uid(rc522_uid_t *uid)
{
    int result = read_uid_internal(uid);
    /* Reset the card's RF session after selection (including failed selection).
       Otherwise a partially selected card can remain READY/ACTIVE for the next poll. */
    if (result != 0) {
        uint8_t antenna;
        if (read_reg(REG_TX_CONTROL, &antenna) ||
            write_reg(REG_TX_CONTROL, (uint8_t)(antenna & 0xFCU))) {
            if (result >= 0) {
                diagnostic.stage = "FIELD RESET";
                diagnostic.reason = "SPI antenna disable failed";
            }
            return -1;
        }
        HAL_Delay(5);
        if (write_reg(REG_TX_CONTROL, (uint8_t)(antenna | 0x03U))) {
            if (result >= 0) {
                diagnostic.stage = "FIELD RESET";
                diagnostic.reason = "SPI antenna enable failed";
            }
            return -1;
        }
        HAL_Delay(5);
        esp_poll();
    }
    return result;
}
