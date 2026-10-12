#include "compact-sender.h"
#include "teletype_io.h"
#include <string.h>
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
pcs_result pcs_quantizer_command(uint8_t address,
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
