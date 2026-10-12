// Checked-transfer mock for OP tests. Adapter lifetime/fault-latch tests are
// separate and link the actual module/ii_checked.c against low-level mocks.
#include "phz_transport.h"
#include "teletype_io.h"
#include <assert.h>
#include <string.h>
phz_test_bus phz_bus;
static pc_response response;
static uint8_t quantizer_response[9];
static int quantizer_pending;
void phz_bus_reset(void) {
    memset(&phz_bus,0,sizeof phz_bus); phz_bus.available=1;
    pc_clear(&response); memset(quantizer_response,0,sizeof quantizer_response);
    quantizer_pending=0;
}
int tele_ii_tx_checked(uint8_t address, const uint8_t *data, uint8_t n) {
    ++phz_bus.calls; phz_bus.address=address;
    if(phz_bus.calls==phz_bus.fail_at) return -8;
    phz_bus.last_length=n;
    if (n>=3 && data[2]==PC_FAMILY_QUANTIZER) {
        assert(pc_qtz_decode(data,n,&phz_bus.last_qtz));
        phz_bus.is_quantizer=1;
        memcpy(quantizer_response,data,6);
        quantizer_response[0]=0x52;
        quantizer_response[6]=phz_bus.disposition ? phz_bus.disposition
                                                 : PC_ADMITTED;
        quantizer_response[7]=quantizer_response[8]=0;
        quantizer_pending=1;
        return 0;
    }
    assert(pc_decode(data,n,&phz_bus.last));
    uint8_t status=phz_bus.disposition;
    uint16_t value=0;
    if(!status) {
        if(phz_bus.last.op<=2) status=PC_ADMITTED;
        else if(phz_bus.last.op==3) value=phz_bus.voltage;
    }
    assert(pc_select(&response,data,n,1,status,value,phz_bus.available));
    return 0;
}
static int copy(void *out,const uint8_t *data,size_t n) {
    assert(n==9); memcpy(out,data,n); return 1;
}
int tele_ii_rx_checked(uint8_t address,uint8_t *data,uint8_t n) {
    assert(address==phz_bus.address && n==9); ++phz_bus.calls;
    if(phz_bus.calls==phz_bus.fail_at) return -8;
    if (quantizer_pending) {
        memcpy(data,quantizer_response,sizeof quantizer_response);
        quantizer_pending=0;
    } else assert(pc_take(&response,1,copy,data));
    if(phz_bus.calls==phz_bus.corrupt_at) data[5]^=1;
    return 0;
}
