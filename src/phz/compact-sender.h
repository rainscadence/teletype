#ifndef PHZ_COMPACT_SENDER_H
#define PHZ_COMPACT_SENDER_H
#include "compact.h"
/* Local outcomes, not public PHZ.ERR numeric allocations. */
enum { PCS_OK, PCS_ARGUMENT, PCS_DISABLED, PCS_TX, PCS_RX, PCS_PROTOCOL,
       PCS_REMOTE, PCS_UNAVAILABLE };
typedef struct {
    int error, driver_error;
    uint8_t remote_status, remote_known;
    uint16_t data;
} pcs_result;
/* Foreground/non-reentrant. No implicit device selection.
 * OUT must use pcs_output so an unvalidated voltage cannot escape to an OP.
 * VALID is internal to that pair. Tags are supplied by the future OP context. */
pcs_result pcs_command(uint8_t address, const pc_request *request);
/* Quantizer writes use the family-5 codec and may transmit up to 16 bytes.
 * CAPTURE_CHORD repeats the same tagged request once: success is the retained
 * terminal receipt; Busy means the foreground apply has not completed yet. */
pcs_result pcs_quantizer_command(uint8_t address,
                                 const pc_qtz_request *request);
typedef struct { int32_t voltage; uint8_t ok; pcs_result result; } pcs_sample;
/* Clears old voltage/validity before any validation or I/O. Signed conversion
 * is explicit; units are unchanged. Caller owns scene/device invalidation. */
void pcs_output(uint8_t address, uint8_t mapping, uint16_t tag, pcs_sample *out);
#endif
