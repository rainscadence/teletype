#ifndef TEST_PHZ_TRANSPORT_H
#define TEST_PHZ_TRANSPORT_H
#include "phz/compact.h"
typedef struct {
    unsigned calls, fail_at, corrupt_at;
    uint8_t address, disposition;
    uint16_t voltage;
    int available;
    pc_request last;
    pc_qtz_request last_qtz;
    uint8_t last_length, is_quantizer;
    uint8_t capture_result, capture_busy, capture_queries;
} phz_test_bus;
extern phz_test_bus phz_bus;
void phz_bus_reset(void);
#endif
