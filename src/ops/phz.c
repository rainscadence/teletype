#include "ops/phz.h"
#include "helpers.h"
#include "teletype.h"
#include "phz/compact-sender.h"
#include <string.h>

// Reuse registered names instead of expanding the large generated lexer.
// Explicit IDs keep recognition independent of enum ordering or later additions.
bool phz_match_token(const char *token, size_t length, tele_data_t *out) {
    static const uint16_t ops[] = {E_OP_PHZ, E_OP_PHZ_ADDR, E_OP_PHZ_ERR,
        E_OP_PHZ_Q_IN, E_OP_PHZ_Q_POS, E_OP_PHZ_Q_OUT, E_OP_PHZ_Q_OK,
        E_OP_PHZ_QTZ_SCALE, E_OP_PHZ_QTZ_ROOT, E_OP_PHZ_QTZ_OCT,
        E_OP_PHZ_QTZ_MASK, E_OP_PHZ_QTZ_RANGE, E_OP_PHZ_QTZ_ROFF,
        E_OP_PHZ_QTZ_REPL};
    static const uint16_t mods[] = {E_MOD_PHZ1,E_MOD_PHZ2,E_MOD_PHZ3,E_MOD_PHZ4};
    for (size_t i=0; i<sizeof ops/sizeof ops[0]; ++i) {
        const char *name=tele_ops[ops[i]]->name;
        if (strlen(name)==length && !memcmp(token,name,length)) {
            out->tag=OP; out->value=ops[i]; return true;
        }
    }
    for (size_t i=0; i<sizeof mods/sizeof mods[0]; ++i) {
        const char *name=tele_mods[mods[i]]->name;
        if (strlen(name)==length && !memcmp(token,name,length)) {
            out->tag=MOD; out->value=mods[i]; return true;
        }
    }
    return false;
}

static uint16_t next_tag(scene_state_t *ss) {
    uint16_t *tag=&ss->phz.tag[ss->phz.selected];
    *tag= *tag==UINT16_MAX ? 1 : (uint16_t)(*tag+1);
    return *tag;
}
static void record(scene_state_t *ss, pcs_result r) {
    ss->phz.error=(int16_t)r.error;
    ss->phz.driver_error=(int16_t)r.driver_error;
    ss->phz.remote_status=r.remote_status;
    ss->phz.remote_known=r.remote_known;
}
static void argument_error(scene_state_t *ss) {
    pcs_result r={0}; r.error=PCS_ARGUMENT; record(ss,r);
}
static void clear_error(scene_state_t *ss) {
    pcs_result r={0}; record(ss,r);
}
static void error_get(const void *NOTUSED(data), scene_state_t *ss,
                      exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t error=PHZ_ERR_PROTOCOL;
    switch (ss->phz.error) {
    case PCS_OK: error=PHZ_ERR_NONE; break;
    case PCS_ARGUMENT: error=PHZ_ERR_ARGUMENT; break;
    case PCS_DISABLED: error=PHZ_ERR_DISABLED; break;
    case PCS_TX: error=PHZ_ERR_SEND; break;
    case PCS_RX: error=PHZ_ERR_RECEIVE; break;
    case PCS_PROTOCOL: break;
    case PCS_UNAVAILABLE: error=PHZ_ERR_UNAVAILABLE; break;
    case PCS_REMOTE:
        if (!ss->phz.remote_known) break;
        switch (ss->phz.remote_status) {
        case PC_ARGUMENT: error=PHZ_ERR_ARGUMENT; break;
        case PC_WRONG_APP: error=PHZ_ERR_WRONG_APP; break;
        case PC_UNAVAILABLE: error=PHZ_ERR_UNAVAILABLE; break;
        case PC_BUSY: error=PHZ_ERR_BUSY; break;
        case PC_FULL: error=PHZ_ERR_QUEUE_FULL; break;
        case PC_UNSUPPORTED: error=PHZ_ERR_UNSUPPORTED; break;
        default: break;
        }
        break;
    }
    // Non-destructive and bus-free; selector restoration preserves the error.
    cs_push(cs,error);
}
static void signal_write(uint8_t op, scene_state_t *ss, command_state_t *cs) {
    int16_t mapping=cs_pop(cs), value=cs_pop(cs);
    if (mapping<1 || mapping>32 || (op==2 && (value<0 || value>16384))) {
        argument_error(ss); return;
    }
    pc_request r={1,op,(uint8_t)mapping,next_tag(ss),(uint16_t)value};
    record(ss,pcs_command(ss->phz.address[ss->phz.selected],&r));
}
static void input(const void *NOTUSED(data), scene_state_t *ss,
                  exec_state_t *NOTUSED(es), command_state_t *cs) {
    signal_write(1,ss,cs);
}
static void position(const void *NOTUSED(data), scene_state_t *ss,
                     exec_state_t *NOTUSED(es), command_state_t *cs) {
    signal_write(2,ss,cs);
}
static void quantizer_send(scene_state_t *ss, pc_qtz_request *request) {
    request->tag=next_tag(ss);
    record(ss,pcs_quantizer_command(ss->phz.address[ss->phz.selected],request));
}
static void quantizer_property(uint8_t op, scene_state_t *ss,
                               command_state_t *cs) {
    int16_t slot=cs_pop(cs), value=cs_pop(cs);
    pc_qtz_request request={0};
    request.op=op;
    if (slot<1 || slot>8) { argument_error(ss); return; }
    request.slot=(uint8_t)slot;
    switch (op) {
    case PC_QTZ_SCALE:
        if (value<0) { argument_error(ss); return; }
        request.scale=(uint16_t)value; break;
    case PC_QTZ_ROOT:
        if (value<0 || value>11) { argument_error(ss); return; }
        request.root=(uint8_t)value; break;
    case PC_QTZ_OCTAVE:
        if (value<-8 || value>8) { argument_error(ss); return; }
        request.octave=(int8_t)value; break;
    case PC_QTZ_MASK:
        if (!value) { argument_error(ss); return; }
        request.mask=(uint16_t)value; break;
    default: argument_error(ss); return;
    }
    quantizer_send(ss,&request);
}
static void quantizer_scale(const void *NOTUSED(data), scene_state_t *ss,
                            exec_state_t *NOTUSED(es), command_state_t *cs) {
    quantizer_property(PC_QTZ_SCALE,ss,cs);
}
static void quantizer_root(const void *NOTUSED(data), scene_state_t *ss,
                           exec_state_t *NOTUSED(es), command_state_t *cs) {
    quantizer_property(PC_QTZ_ROOT,ss,cs);
}
static void quantizer_octave(const void *NOTUSED(data), scene_state_t *ss,
                             exec_state_t *NOTUSED(es), command_state_t *cs) {
    quantizer_property(PC_QTZ_OCTAVE,ss,cs);
}
static void quantizer_mask(const void *NOTUSED(data), scene_state_t *ss,
                           exec_state_t *NOTUSED(es), command_state_t *cs) {
    quantizer_property(PC_QTZ_MASK,ss,cs);
}
static void quantizer_range(const void *NOTUSED(data), scene_state_t *ss,
                            exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t slot=cs_pop(cs), low=cs_pop(cs), high=cs_pop(cs);
    if (slot<1 || slot>8 || low<-8 || low>8 || high<-8 || high>8 || low>high) {
        argument_error(ss); return;
    }
    pc_qtz_request request={0};
    request.op=PC_QTZ_RANGE; request.slot=(uint8_t)slot;
    request.low=(int8_t)low; request.high=(int8_t)high;
    quantizer_send(ss,&request);
}
static void quantizer_range_off(const void *NOTUSED(data), scene_state_t *ss,
                                exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t slot=cs_pop(cs);
    if (slot<1 || slot>8) { argument_error(ss); return; }
    pc_qtz_request request={0};
    request.op=PC_QTZ_RANGE_OFF; request.slot=(uint8_t)slot;
    request.low=1; request.high=0;
    quantizer_send(ss,&request);
}
static void quantizer_replace(const void *NOTUSED(data), scene_state_t *ss,
                              exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t slot=cs_pop(cs), scale=cs_pop(cs), root=cs_pop(cs);
    int16_t octave=cs_pop(cs), mask=cs_pop(cs), enabled=cs_pop(cs);
    int16_t low=cs_pop(cs), high=cs_pop(cs);
    if (slot<1 || slot>8 || scale<0 || root<0 || root>11 ||
        octave<-8 || octave>8 || !mask || enabled<0 || enabled>1 ||
        (enabled ? (low<-8 || low>8 || high<-8 || high>8 || low>high)
                 : (low!=1 || high!=0))) {
        argument_error(ss); return;
    }
    pc_qtz_request request={0};
    request.op=PC_QTZ_REPLACE; request.slot=(uint8_t)slot;
    request.scale=(uint16_t)scale; request.root=(uint8_t)root;
    request.octave=(int8_t)octave; request.mask=(uint16_t)mask;
    request.range_enabled=(uint8_t)enabled;
    request.low=(int8_t)low; request.high=(int8_t)high;
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

static void select_get(const void *NOTUSED(data), scene_state_t *ss,
                       exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, ss->phz.selected + 1);
}
static void select_set(const void *NOTUSED(data), scene_state_t *ss,
                       exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t slot=cs_pop(cs);
    if (slot < 1 || slot > 4) { argument_error(ss); return; }
    ss->phz.output_valid=0;
    ss->phz.selected=(uint8_t)(slot-1);
    clear_error(ss);
}
static void address_get(const void *NOTUSED(data), scene_state_t *ss,
                        exec_state_t *NOTUSED(es), command_state_t *cs) {
    cs_push(cs, ss->phz.address[ss->phz.selected]);
}
static void address_set(const void *NOTUSED(data), scene_state_t *ss,
                        exec_state_t *NOTUSED(es), command_state_t *cs) {
    int16_t address=cs_pop(cs);
    if (address != 0 && (address < 8 || address > 0x77)) { argument_error(ss); return; }
    ss->phz.output_valid=0;
    ss->phz.address[ss->phz.selected]=(uint8_t)address;
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
const tele_op_t op_PHZ=MAKE_GET_SET_OP(PHZ,select_get,select_set,0,true);
const tele_op_t op_PHZ_ADDR=MAKE_GET_SET_OP(PHZ.ADDR,address_get,address_set,0,true);
const tele_op_t op_PHZ_ERR=MAKE_GET_OP(PHZ.ERR,error_get,0,true);
const tele_op_t op_PHZ_Q_IN=MAKE_GET_OP(PHZ.Q.IN,input,2,false);
const tele_op_t op_PHZ_Q_POS=MAKE_GET_OP(PHZ.Q.POS,position,2,false);
const tele_op_t op_PHZ_Q_OUT=MAKE_GET_OP(PHZ.Q.OUT,output,1,true);
const tele_op_t op_PHZ_Q_OK=MAKE_GET_OP(PHZ.Q.OK,output_ok,0,true);
const tele_op_t op_PHZ_QTZ_SCALE=MAKE_GET_OP(PHZ.QTZ.SCALE,quantizer_scale,2,false);
const tele_op_t op_PHZ_QTZ_ROOT=MAKE_GET_OP(PHZ.QTZ.ROOT,quantizer_root,2,false);
const tele_op_t op_PHZ_QTZ_OCT=MAKE_GET_OP(PHZ.QTZ.OCT,quantizer_octave,2,false);
const tele_op_t op_PHZ_QTZ_MASK=MAKE_GET_OP(PHZ.QTZ.MASK,quantizer_mask,2,false);
const tele_op_t op_PHZ_QTZ_RANGE=MAKE_GET_OP(PHZ.QTZ.RANGE,quantizer_range,3,false);
const tele_op_t op_PHZ_QTZ_ROFF=MAKE_GET_OP(PHZ.QTZ.ROFF,quantizer_range_off,1,false);
const tele_op_t op_PHZ_QTZ_REPL=MAKE_GET_OP(PHZ.QTZ.REPL,quantizer_replace,8,false);
const tele_mod_t mod_PHZ1=MAKE_MOD(PHZ1,scope1,0);
const tele_mod_t mod_PHZ2=MAKE_MOD(PHZ2,scope2,0);
const tele_mod_t mod_PHZ3=MAKE_MOD(PHZ3,scope3,0);
const tele_mod_t mod_PHZ4=MAKE_MOD(PHZ4,scope4,0);
