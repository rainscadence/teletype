// Standalone host test of the actual module adapter, with the driver replaced.
// Each fault scenario runs in a fresh process, like fresh boot static storage.
#include "teletype_io.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int calls, result;
static uint8_t *retained;
static uint8_t expected[16];
int i2c_leader_tx(uint8_t addr, uint8_t *data, uint8_t length) {
    assert(addr==69 && length==16);
    assert(!memcmp(data,expected,16)); retained=data; ++calls; return result;
}
int i2c_leader_rx(uint8_t addr, uint8_t *data, uint8_t length) {
    assert(addr==69 && length==9);
    for(unsigned i=0;i<9;++i) assert(data[i]==0);
    memcpy(data,expected,9); retained=data; ++calls; return result;
}
int main(int argc, char **argv) {
    assert(argc==3);
    int receive=atoi(argv[1]); result=atoi(argv[2]);
    uint8_t user[16]; memset(user,0xa5,16); memset(expected,0xa5,16);
    assert(tele_ii_tx_checked(0,user,9)==-1);
    assert(tele_ii_rx_checked(0x78,user,9)==-1);
    assert(tele_ii_tx_checked(69,0,9)==-1);
    assert(tele_ii_rx_checked(69,user,0)==-1);
    assert(tele_ii_tx_checked(69,user,17)==-1 && calls==0);
    if(receive) memset(expected,0x42,9);
    int status=receive ? tele_ii_rx_checked(69,user,9) : tele_ii_tx_checked(69,user,16);
    assert(status==result && calls==1 && retained!=user);
    if(result) {
        for(unsigned i=0;i<16;++i) assert(user[i]==0xa5);
        // Model a late IRQ access after the driver's timeout return.
        if(receive) memset(retained,0x77,9);
        else { memset(user,0,16); assert(!memcmp(retained,expected,16)); }
        assert(tele_ii_tx_checked(69,user,16)==result);
        assert(tele_ii_rx_checked(69,user,9)==result);
        assert(calls==1); // No retry, read-after-failed-write or buffer reuse.
    } else {
        if(receive) assert(!memcmp(user,expected,9));
        memcpy(user,expected,16);
        assert(tele_ii_tx_checked(69,user,16)==0 && calls==2);
    }
    puts("PASS checked adapter scenario");
}
