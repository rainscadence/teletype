#include "ops/phz.h"
#include "helpers.h"
#include "teletype.h"
#include "phz/compact-sender.h"
#include <string.h>
#if defined(__AVR32__)
#define PHZ_NOINLINE __attribute__((noinline))
#define PHZ_NO_JUMPS __attribute__((optimize("no-jump-tables")))
// Keep this one immutable descriptor in the measured pre-vector flash gap.
// The AVR32 has unified flash addressing; this avoids spending scarce bytes
// after the fixed exception-vector alignment without moving the NVRAM boundary.
#define PHZ_PREVECTOR_DATA __attribute__((section(".text.phz_prevector")))
#else
#define PHZ_NOINLINE
#define PHZ_NO_JUMPS
#define PHZ_PREVECTOR_DATA
#endif

// Reuse the contiguous PHZ registration ranges instead of expanding the large
// generated lexer.
bool phz_match_token(const char *token, size_t length, tele_data_t *out) {
    for (uint16_t i=E_OP_PHZ; i<=E_OP_PHZ_I2M_C; ++i) {
        const char *name=tele_ops[i]->name;
        if (!strncmp(token,name,length) && !name[length]) {
            out->tag=OP; out->value=i; return true;
        }
    }
    if (length==4 && !memcmp(token,"PHZ",3) &&
        token[3]>='1' && token[3]<='4') {
        out->tag=MOD;
        out->value=(int16_t)(E_MOD_PHZ1+token[3]-'1');
        return true;
    }
    return false;
}
static PHZ_NOINLINE uint16_t next_tag(scene_state_t *ss);
static PHZ_NOINLINE void record(scene_state_t *ss, pcs_result r);
static PHZ_NOINLINE void argument_error(scene_state_t *ss);
static void chord_setting(const void *data, scene_state_t *ss,
                          exec_state_t *NOTUSED(es), command_state_t *cs) {
    uint8_t op=(uint8_t)(uintptr_t)data;
    int16_t chord=cs_pop(cs), value=cs_pop(cs);
    if (chord<0 || chord>8 ||
        (op==PC_MIDI_CHORD_SCALE ? (value<0 || value>8) :
         (value<-127 || value>127))) {
        argument_error(ss); return;
    }
    pc_request request={PC_FAMILY_MIDI,op,(uint8_t)chord,next_tag(ss),
                        (uint16_t)value};
    record(ss,pcs_command(ss->phz.address[ss->phz.selected],&request));
}
static void chord_play(const void *NOTUSED(data), scene_state_t *ss,
                       exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t chord=cs_pop(cs), root=cs_pop(cs), velocity=cs_pop(cs);
    uint8_t channel=ss->phz.channel[ss->phz.selected];
    if (chord<1 || chord>8 || root<0 || root>127 || velocity<0 ||
        velocity>127 || channel<1 || channel>16) {
        argument_error(ss); return;
    }
    pc_request request={
        PC_FAMILY_MIDI,PC_MIDI_CHORD_PLAY,
        (uint8_t)(((channel-1)<<4)|chord),next_tag(ss),
        (uint16_t)((root<<8)|velocity)};
    record(ss,pcs_command(ss->phz.address[ss->phz.selected],&request));
}

static PHZ_NOINLINE uint16_t next_tag(scene_state_t *ss) {
    uint16_t *tag=&ss->phz.tag[ss->phz.selected];
    *tag= *tag==UINT16_MAX ? 1 : (uint16_t)(*tag+1);
    return *tag;
}
static PHZ_NOINLINE void record(scene_state_t *ss, pcs_result r) {
    ss->phz.error=(int16_t)r.error;
    ss->phz.driver_error=(int16_t)r.driver_error;
    ss->phz.remote_status=r.remote_status;
    ss->phz.remote_known=r.remote_known;
}
static PHZ_NOINLINE void argument_error(scene_state_t *ss) {
    pcs_result r={0}; r.error=PCS_ARGUMENT; record(ss,r);
}
static void clear_error(scene_state_t *ss) {
    pcs_result r={0}; record(ss,r);
}
static void error_get(const void *NOTUSED(data), scene_state_t *ss,
                      exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t error=ss->phz.error;
    if (error==PCS_REMOTE) {
        uint8_t status=ss->phz.remote_status;
        /* PCS_REMOTE is only created after a validated, known error reply. */
        error=status==PC_ARGUMENT ? PHZ_ERR_ARGUMENT : status+3;
    }
    // Non-destructive and bus-free; selector restoration preserves the error.
    cs_push(cs,error);
}
static void signal_write(const void *data, scene_state_t *ss,
                         exec_state_t *NOTUSED(es), command_state_t *cs) {
    uint8_t op=(uint8_t)(uintptr_t)data;
    uint8_t family=PC_FAMILY_MIDI;
    int16_t mapping=cs_pop(cs), value=cs_pop(cs);
    if (op) {
        family=PC_FAMILY_QUADRANTS;
        if (mapping<1 || mapping>32 ||
            (op==2 && (value<0 || value>16384))) goto invalid;
    } else {
        op=PC_MIDI_CHORD_BITMAP;
        if (mapping<0 || mapping>8) goto invalid;
    }
    pc_request r={family,op,(uint8_t)mapping,next_tag(ss),(uint16_t)value};
    record(ss,pcs_command(ss->phz.address[ss->phz.selected],&r));
    return;
invalid:
    argument_error(ss);
}
static void quantizer_send(scene_state_t *ss, pc_qtz_request *request) {
    request->tag=next_tag(ss);
    record(ss,pcs_quantizer_command(ss->phz.address[ss->phz.selected],request));
}
static PHZ_NOINLINE bool quantizer_range(int16_t low, int16_t high) {
    return (uint16_t)(low+8)<=16 && (uint16_t)(high+8)<=16 && low<=high;
}
static PHZ_NO_JUMPS void quantizer(const void *data, scene_state_t *ss,
                                   exec_state_t *NOTUSED(es),
                                   command_state_t *cs) {
    uint8_t op=(uint8_t)(uintptr_t)data;
    int16_t slot=cs_pop(cs), value[7];
    uint8_t count=op==PC_QTZ_RANGE_OFF ? 0 :
                  op==PC_QTZ_RANGE ? 2 : op==PC_QTZ_REPLACE ? 7 : 1;
    pc_qtz_request request;
    for (uint8_t i=0; i<count; ++i) value[i]=cs_pop(cs);
    request.op=op;
    if (slot<1 || slot>8) { argument_error(ss); return; }
    request.slot=(uint8_t)slot;
    switch (op) {
    case PC_QTZ_SCALE:
        if (value[0]<0) { argument_error(ss); return; }
        request.scale=(uint16_t)value[0]; break;
    case PC_QTZ_ROOT:
        if (value[0]<0 || value[0]>11) { argument_error(ss); return; }
        request.root=(uint8_t)value[0]; break;
    case PC_QTZ_OCTAVE:
        if (value[0]<-8 || value[0]>8) { argument_error(ss); return; }
        request.octave=(int8_t)value[0]; break;
    case PC_QTZ_MASK:
        if (!value[0]) { argument_error(ss); return; }
        request.mask=(uint16_t)value[0]; break;
    case PC_QTZ_RANGE: {
        int16_t low=value[0], high=value[1];
        if (!quantizer_range(low,high)) {
            argument_error(ss); return;
        }
        request.low=(int8_t)low; request.high=(int8_t)high; break;
    }
    case PC_QTZ_RANGE_OFF:
        request.low=1; request.high=0; break;
    case PC_QTZ_REPLACE: {
        int16_t scale=value[0], root=value[1], octave=value[2];
        int16_t mask=value[3], enabled=value[4], low=value[5];
        int16_t high=value[6];
        if (scale<0 || root<0 || root>11 || octave<-8 || octave>8 ||
            !mask || enabled<0 || enabled>1 ||
            (enabled ? !quantizer_range(low,high)
                     : (low!=1 || high!=0))) {
            argument_error(ss); return;
        }
        request.scale=(uint16_t)scale; request.root=(uint8_t)root;
        request.octave=(int8_t)octave; request.mask=(uint16_t)mask;
        request.range_enabled=(uint8_t)enabled;
        request.low=(int8_t)low; request.high=(int8_t)high; break;
    }
    case PC_QTZ_CAPTURE_CHORD:
        request.root=(uint8_t)value[0]; break;
    default: argument_error(ss); return;
    }
    quantizer_send(ss,&request);
}
static void output(const void *NOTUSED(data), scene_state_t *ss,
                   exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t mapping=cs_pop(cs);
    ss->phz.output_valid=0;
    if (mapping<1 || mapping>32) { argument_error(ss); cs_push(cs,0); return; }
    pcs_sample sample;
    pcs_output(ss->phz.address[ss->phz.selected],(uint8_t)mapping,next_tag(ss),&sample);
    record(ss,sample.result);
    ss->phz.output_valid=sample.ok;
    cs_push(cs,(int16_t)sample.voltage);
}
static void output_ok(const void *NOTUSED(data), scene_state_t *ss,
                      exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs,ss->phz.output_valid);
}

static void setting_get(const void *data, scene_state_t *ss,
                        exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs,(uintptr_t)data ? ss->phz.address[ss->phz.selected]
                              : ss->phz.selected+1);
}
static void setting_set(const void *data, scene_state_t *ss,
                        exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t value=cs_pop(cs);
    if ((uintptr_t)data ? (value!=0 && (value<8 || value>0x77))
                        : (value<1 || value>4)) {
        argument_error(ss); return;
    }
    ss->phz.output_valid=0;
    if ((uintptr_t)data) ss->phz.address[ss->phz.selected]=(uint8_t)value;
    else ss->phz.selected=(uint8_t)(value-1);
    clear_error(ss);
}
static void scoped(uint8_t slot, scene_state_t *ss, exec_state_t *es,
                   const tele_command_t *command) {
    uint8_t saved=ss->phz.selected;
    uint8_t outer_scope=ss->phz.scope_active;
    ss->phz.scope_active=1;
    ss->phz.output_valid=0;
    ss->phz.selected=slot;
    process_command(ss,es,command);
    // Scene reset clears scope_active; never restore an old scene's selection.
    if (ss->phz.scope_active) {
        ss->phz.selected=saved;
        ss->phz.scope_active=outer_scope;
    }
    ss->phz.output_valid=0;
}
#define PHZ_SCOPE(N, SLOT) \
static void scope##N(scene_state_t *ss, exec_state_t *es, \
                     command_state_t *NOTUSED(cs), const tele_command_t *c) { \
    scoped(SLOT,ss,es,c); \
}
PHZ_SCOPE(1,0)
PHZ_SCOPE(2,1)
PHZ_SCOPE(3,2)
PHZ_SCOPE(4,3)
#define PHZ_GET_OP(n,g,p,r,d) \
    { .name=#n, .get=g, .set=NULL, .params=p, .returns=r, \
      .data=(const void *)(uintptr_t)(d) }
#define PHZ_GET_SET_OP(n,g,s,p,r,d) \
    { .name=#n, .get=g, .set=s, .params=p, .returns=r, \
      .data=(const void *)(uintptr_t)(d) }
const tele_op_t op_PHZ=PHZ_GET_SET_OP(PHZ,setting_get,setting_set,0,true,0);
const tele_op_t op_PHZ_ADDR=PHZ_GET_SET_OP(PHZ.ADDR,setting_get,setting_set,0,true,1);
const tele_op_t op_PHZ_ERR=MAKE_GET_OP(PHZ.ERR,error_get,0,true);
const tele_op_t op_PHZ_Q_IN=PHZ_GET_OP(PHZ.Q.IN,signal_write,2,false,1);
const tele_op_t op_PHZ_Q_POS=PHZ_GET_OP(PHZ.Q.POS,signal_write,2,false,2);
const tele_op_t op_PHZ_Q_OUT=MAKE_GET_OP(PHZ.Q.OUT,output,1,true);
const tele_op_t op_PHZ_Q_OK=MAKE_GET_OP(PHZ.Q.OK,output_ok,0,true);
const tele_op_t op_PHZ_QTZ_SCALE=PHZ_GET_OP(PHZ.QTZ.SCALE,quantizer,2,false,PC_QTZ_SCALE);
const tele_op_t op_PHZ_QTZ_ROOT=PHZ_GET_OP(PHZ.QTZ.ROOT,quantizer,2,false,PC_QTZ_ROOT);
const tele_op_t op_PHZ_QTZ_OCT=PHZ_GET_OP(PHZ.QTZ.OCT,quantizer,2,false,PC_QTZ_OCTAVE);
const tele_op_t op_PHZ_QTZ_MASK=PHZ_GET_OP(PHZ.QTZ.MASK,quantizer,2,false,PC_QTZ_MASK);
const tele_op_t op_PHZ_QTZ_RANGE=PHZ_GET_OP(PHZ.QTZ.RANGE,quantizer,3,false,PC_QTZ_RANGE);
const tele_op_t op_PHZ_QTZ_ROFF=PHZ_GET_OP(PHZ.QTZ.ROFF,quantizer,1,false,PC_QTZ_RANGE_OFF);
const tele_op_t op_PHZ_QTZ_REPL=PHZ_GET_OP(PHZ.QTZ.REPL,quantizer,8,false,PC_QTZ_REPLACE);
const tele_op_t op_PHZ_I2M_C_B PHZ_PREVECTOR_DATA=
    PHZ_GET_OP(PHZ.I2M.C.B,signal_write,2,false,0);
const tele_op_t op_PHZ_QTZ_CAP PHZ_PREVECTOR_DATA=
    PHZ_GET_OP(PHZ.QTZ.CAP,quantizer,2,false,PC_QTZ_CAPTURE_CHORD);
const tele_op_t op_PHZ_I2M_C_SC PHZ_PREVECTOR_DATA=
    PHZ_GET_OP(PHZ.I2M.C.SC,chord_setting,2,false,PC_MIDI_CHORD_SCALE);
const tele_op_t op_PHZ_I2M_C_TRP PHZ_PREVECTOR_DATA=
    PHZ_GET_OP(PHZ.I2M.C.TRP,chord_setting,2,false,PC_MIDI_CHORD_TRANSPOSE);
const tele_op_t op_PHZ_I2M_C PHZ_PREVECTOR_DATA=
    PHZ_GET_OP(PHZ.I2M.C,chord_play,3,false,0);
const tele_mod_t mod_PHZ1=MAKE_MOD(PHZ1,scope1,0);
const tele_mod_t mod_PHZ2=MAKE_MOD(PHZ2,scope2,0);
const tele_mod_t mod_PHZ3=MAKE_MOD(PHZ3,scope3,0);
const tele_mod_t mod_PHZ4 PHZ_PREVECTOR_DATA=MAKE_MOD(PHZ4,scope4,0);
