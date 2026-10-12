#include "teletype_io.h"
#include "i2c.h"
#include <string.h>

// The pinned TWI driver can retain its buffer pointer after TWI_BUSY. Keep
// storage alive and do not reuse it after any driver failure. There is no scene
// reset hook: only reboot clears this conservative PHZ-only fault latch.
static uint8_t tx_buffer[16];
static uint8_t rx_buffer[16];
static int fault;

#if !defined(__AVR32__)
// Host callers exercise the public adapter contract with arbitrary arguments.
// AVR32 callers are the fixed-size internal PHZ transactions, whose address is
// checked at the sender boundary; omitting duplicate checks keeps NVRAM fixed.
static int valid(uint8_t addr, const void *data, uint8_t length) {
    return addr >= 0x08 && addr <= 0x77 && data && length && length <= 16;
}
#endif

int tele_ii_tx_checked(uint8_t addr, const uint8_t *data, uint8_t length) {
#if !defined(__AVR32__)
    if (!valid(addr, data, length)) return -1; // TWI_INVALID_ARGUMENT
#endif
    if (fault) return fault;
    memcpy(tx_buffer, data, length);
    int status = i2c_leader_tx(addr, tx_buffer, length);
    if (status) fault = status;
    return status;
}

int tele_ii_rx_checked(uint8_t addr, uint8_t *data, uint8_t length) {
#if !defined(__AVR32__)
    if (!valid(addr, data, length)) return -1;
#endif
    if (fault) return fault;
    memset(rx_buffer, 0, sizeof rx_buffer);
    int status = i2c_leader_rx(addr, rx_buffer, length);
    if (status) { fault = status; return status; }
    memcpy(data, rx_buffer, length);
    return 0;
}
