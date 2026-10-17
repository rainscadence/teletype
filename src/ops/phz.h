#ifndef OPS_PHZ_H
#define OPS_PHZ_H
#include "ops/op.h"
bool phz_match_token(const char *token, size_t length, tele_data_t *out);
extern const tele_op_t op_PHZ;
extern const tele_op_t op_PHZ_ADDR;
extern const tele_op_t op_PHZ_ERR;
// Candidate public PHZ.ERR values; independent of driver and wire statuses.
// Last PHZ signal attempt or explicit selection/address setter wins. Successful
// setters/exchanges clear it. PHZ/PHZ.ADDR getters, Q.OK and ERR preserve it.
// Temporary selection/restoration preserves the body's result; scene init clears.
// NONE after a write means admitted, not applied. Parser errors use Teletype's
// existing error display and never execute the OP or replace this error.
// No load/dependency statuses yet: setup commands are not implemented here.
enum {
    PHZ_ERR_NONE=0, PHZ_ERR_ARGUMENT=1, PHZ_ERR_DISABLED=2,
    PHZ_ERR_SEND=3, PHZ_ERR_RECEIVE=4, PHZ_ERR_PROTOCOL=5,
    PHZ_ERR_WRONG_APP=6, PHZ_ERR_UNAVAILABLE=7, PHZ_ERR_BUSY=8,
    PHZ_ERR_QUEUE_FULL=9, PHZ_ERR_UNSUPPORTED=10
};
extern const tele_op_t op_PHZ_Q_IN, op_PHZ_Q_POS, op_PHZ_Q_OUT, op_PHZ_Q_OK;
extern const tele_op_t op_PHZ_QTZ_SCALE, op_PHZ_QTZ_ROOT, op_PHZ_QTZ_OCT;
extern const tele_op_t op_PHZ_QTZ_MASK, op_PHZ_QTZ_RANGE, op_PHZ_QTZ_ROFF;
extern const tele_op_t op_PHZ_QTZ_REPL;
extern const tele_op_t op_PHZ_I2M_C_B;
extern const tele_mod_t mod_PHZ1, mod_PHZ2, mod_PHZ3, mod_PHZ4;
#endif
