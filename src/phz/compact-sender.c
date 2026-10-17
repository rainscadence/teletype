#include "compact-sender.h"
#include "teletype_io.h"
#include <string.h>
#if defined(__AVR32__)
// The AVR32 image has only a few bytes of flash headroom. OP handlers validate
// every field before reaching this internal path; keep framing and reply checks
// here, while the host path below retains the standalone codec validation.
#define PCS_NOINLINE __attribute__((noinline))
#define PCS_NO_JUMPS __attribute__((optimize("no-jump-tables")))
static void put_word(uint8_t *b, uint16_t value) {
    b[0]=(uint8_t)(value >> 8); b[1]=(uint8_t)value;
}
static uint16_t word(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] << 8 | b[1]);
}
static PCS_NOINLINE void result_clear(pcs_result *r) {
    memset(r,0,sizeof *r);
}
static PCS_NOINLINE void request_header(uint8_t *tx, uint8_t family, uint8_t op,
                                        uint16_t tag) {
    tx[0]=0x50; tx[1]=1; tx[2]=family; tx[3]=op; put_word(tx+4,tag);
}
static void exchange(uint8_t address, const uint8_t *tx, uint8_t length,
                     uint8_t family, uint8_t op, uint16_t tag,
                     uint8_t reply_kind, pcs_result *r) {
    uint8_t rx[9];
    uint16_t value;
again:
    result_clear(r);
    if (!address) { r->error=PCS_DISABLED; return; }
    if (address<8 || address>0x77) { r->error=PCS_ARGUMENT; return; }
    r->driver_error=tele_ii_tx_checked(address,tx,length);
    if (r->driver_error) { r->error=PCS_TX; return; }
    r->driver_error=tele_ii_rx_checked(address,rx,sizeof rx);
    if (r->driver_error) { r->error=PCS_RX; return; }
    value=word(rx+7);
    if (rx[0]!=0x52 || rx[1]!=1 || rx[2]!=family || rx[3]!=op ||
        word(rx+4)!=tag || rx[6]>PC_UNSUPPORTED ||
        (rx[6]>=PC_ARGUMENT ? value!=0 :
         (!(reply_kind & 1) ? (rx[6]!=PC_ADMITTED || value!=0) :
          (rx[6]!=PC_READ || (reply_kind==3 && value>1))))) {
        r->error=PCS_PROTOCOL; return;
    }
    r->remote_status=rx[6]; r->data=value; r->remote_known=1;
    if (r->remote_status>=PC_ARGUMENT) r->error=PCS_REMOTE;
    if (reply_kind==2 && !r->error) { reply_kind=0; goto again; }
}
pcs_result pcs_command(uint8_t address, const pc_request *request) {
    pcs_result r; uint8_t tx[9];
    request_header(tx,request->family,request->op,request->tag);
    tx[6]=request->mapping; put_word(tx+7,request->value);
    exchange(address,tx,9,request->family,request->op,request->tag,0,&r);
    return r;
}
PCS_NO_JUMPS pcs_result pcs_quantizer_command(
    uint8_t address, const pc_qtz_request *request) {
    pcs_result r; uint8_t tx[16], length=9;
    request_header(tx,PC_FAMILY_QUANTIZER,request->op,request->tag);
    tx[6]=request->slot;
    switch (request->op) {
    case PC_QTZ_SCALE:
        put_word(tx+7,request->scale); break;
    case PC_QTZ_ROOT:
    case PC_QTZ_CAPTURE_CHORD:
        tx[7]=request->root; tx[8]=0; break;
    case PC_QTZ_OCTAVE:
        tx[7]=(uint8_t)request->octave; tx[8]=0; break;
    case PC_QTZ_MASK:
        put_word(tx+7,request->mask); break;
    case PC_QTZ_RANGE:
        tx[7]=(uint8_t)request->low; tx[8]=(uint8_t)request->high; break;
    case PC_QTZ_RANGE_OFF:
        length=7; break;
    case PC_QTZ_REPLACE:
        length=16; put_word(tx+7,request->scale); tx[9]=request->root;
        tx[10]=(uint8_t)request->octave; put_word(tx+11,request->mask);
        tx[13]=request->range_enabled; tx[14]=(uint8_t)request->low;
        tx[15]=(uint8_t)request->high; break;
    default: break;
    }
    exchange(address,tx,length,PC_FAMILY_QUANTIZER,request->op,
             request->tag,request->op==PC_QTZ_CAPTURE_CHORD ? 2 : 0,&r);
    return r;
}
void pcs_output(uint8_t address, uint8_t mapping, uint16_t tag, pcs_sample *out) {
    uint8_t tx[9]; uint16_t voltage;
    memset(out,0,sizeof *out);
    request_header(tx,1,3,tag); tx[6]=mapping;
    exchange(address,tx,7,1,3,tag,1,&out->result);
    if (out->result.error) return;
    voltage=out->result.data; out->result.data=0; tx[3]=4;
    exchange(address,tx,7,1,4,tag,3,&out->result);
    if (out->result.error) return;
    if (out->result.data!=1) { out->result.error=PCS_UNAVAILABLE; return; }
    out->voltage=voltage<=32767 ? (int32_t)voltage : (int32_t)voltage-65536;
    out->ok=1;
}
#else
static void exchange(uint8_t address, const pc_request *request, pcs_result *r) {
    memset(r,0,sizeof *r);
    uint8_t tx[9], rx[9]={0}; size_t n=0;
    if (!address) { r->error=PCS_DISABLED; return; }
    if (address<8 || address>0x77 || !pc_encode(request,tx,sizeof tx,&n)) {
        r->error=PCS_ARGUMENT; return;
    }
    r->driver_error=tele_ii_tx_checked(address,tx,(uint8_t)n);
    if (r->driver_error) { r->error=PCS_TX; return; }
    r->driver_error=tele_ii_rx_checked(address,rx,sizeof rx);
    if (r->driver_error) { r->error=PCS_RX; return; }
    if (!pc_match(request,rx,sizeof rx,&r->remote_status,&r->data)) {
        r->error=PCS_PROTOCOL; return;
    }
    r->remote_known=1;
    if (r->remote_status>=PC_ARGUMENT) r->error=PCS_REMOTE;
    return;
}
pcs_result pcs_command(uint8_t address, const pc_request *request) {
    if (request && request->family==1 && request->op>=3) {
        pcs_result r={0}; r.error=PCS_ARGUMENT; return r;
    }
    pcs_result r;
    exchange(address,request,&r);
    return r;
}
static pcs_result quantizer_exchange(uint8_t address,
                                     const pc_qtz_request *request) {
    pcs_result r;
    uint8_t tx[16], rx[9]={0};
    size_t n=0;
    memset(&r,0,sizeof r);
    if (!address) { r.error=PCS_DISABLED; return r; }
    if (address<8 || address>0x77 ||
        !pc_qtz_encode(request,tx,sizeof tx,&n)) {
        r.error=PCS_ARGUMENT; return r;
    }
    r.driver_error=tele_ii_tx_checked(address,tx,(uint8_t)n);
    if (r.driver_error) { r.error=PCS_TX; return r; }
    r.driver_error=tele_ii_rx_checked(address,rx,sizeof rx);
    if (r.driver_error) { r.error=PCS_RX; return r; }
    if (!pc_qtz_match(request,rx,sizeof rx,&r.remote_status)) {
        r.error=PCS_PROTOCOL; return r;
    }
    r.remote_known=1;
    if (r.remote_status>=PC_ARGUMENT) r.error=PCS_REMOTE;
    return r;
}
pcs_result pcs_quantizer_command(uint8_t address,
                                 const pc_qtz_request *request) {
    pcs_result r=quantizer_exchange(address,request);
    if (request && request->op==PC_QTZ_CAPTURE_CHORD && !r.error) {
      r=quantizer_exchange(address,request);
    }
    return r;
}
void pcs_output(uint8_t address, uint8_t mapping, uint16_t tag, pcs_sample *out) {
    pc_request request={1,3,mapping,tag,0};
    uint16_t voltage;
    memset(out,0,sizeof *out);
    exchange(address,&request,&out->result);
    if (out->result.error) return;
    voltage=out->result.data;
    out->result.data=0;
    request.op=4;
    exchange(address,&request,&out->result);
    if (out->result.error) return;
    if (out->result.data!=1) { out->result.error=PCS_UNAVAILABLE; return; }
    out->voltage=voltage<=32767 ? (int32_t)voltage : (int32_t)voltage-65536;
    out->ok=1;
}
#endif
