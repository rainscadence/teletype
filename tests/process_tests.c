#include "process_tests.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // ssize_t

#include "greatest/greatest.h"
#include "teletype.h"
#include "phz_transport.h"
#include "phz/compact-sender.h"
#include "ops/phz.h"
// runs multiple lines of commands and then asserts that the final answer is
// correct (allows contiuation of state)
TEST process_helper_state(scene_state_t* ss, size_t n, char* lines[],
                          int16_t answer) {
    process_result_t result = { .has_value = false, .value = 0 };
    exec_state_t es;
    es_init(&es);
    es_push(&es);
    es_variables(&es)->script_number = 0;
    for (size_t i = 0; i < n; i++) {
        tele_command_t cmd;
        char error_msg[TELE_ERROR_MSG_LENGTH];
        error_t error = parse(lines[i], &cmd, error_msg);
        if (error != E_OK) { FAIL(); }
        if (validate(&cmd, error_msg) != E_OK) { FAIL(); }
        result = process_command(ss, &es, &cmd);
    }

    ASSERT_EQ(result.has_value, true);

    // prep a message for the test
    ssize_t size = 0;
    for (size_t i = 0; i < n; i++) {
        size += strlen(lines[i]) + 3;  // 3 extra chars fo ' | '
    }
    char* message = calloc(size + 1, sizeof(char));
    for (size_t i = 0; i < n; i++) {
        strcat(message, lines[i]);
        if (i < n - 1) strcat(message, " | ");
    }

    ASSERT_EQm(message, result.value, answer);

    free(message);

    PASS();
}


// runs multiple lines of commands and then asserts that the final answer is
// correct
TEST process_helper(size_t n, char* lines[], int16_t answer) {
    scene_state_t ss;
    ss_init(&ss);

    CHECK_CALL(process_helper_state(&ss, n, lines, answer));

    PASS();
}

TEST test_numbers() {
    char* test1[1] = { "1" };
    CHECK_CALL(process_helper(1, test1, 1));

    char* test2[1] = { "2" };
    CHECK_CALL(process_helper(1, test2, 2));

    char* test3[1] = { "0" };
    CHECK_CALL(process_helper(1, test3, 0));

    char* test4[1] = { "-1" };
    CHECK_CALL(process_helper(1, test4, -1));

    PASS();
}

TEST test_ADD() {
    char* test1[1] = { "ADD 5 6" };
    CHECK_CALL(process_helper(1, test1, 11));

    char* test2[1] = { "ADD 2 -2" };
    CHECK_CALL(process_helper(1, test2, 0));

    PASS();
}

TEST test_PROB() {
    char* test1[3] = { "X 0", "PROB 100: X + X 1", "X" };
    for (int i = 0; i < 1000; i++) { CHECK_CALL(process_helper(3, test1, 1)); }

    PASS();
}

TEST test_IF() {
    char* test1[3] = { "X 0", "IF 1: X 1", "X" };
    CHECK_CALL(process_helper(3, test1, 1));

    char* test2[3] = { "X 0", "IF 0: X 1", "X" };
    CHECK_CALL(process_helper(3, test2, 0));

    char* test3[3] = { "PN 0 0 0", "IF 1: PN 0 0 1", "PN 0 0" };
    CHECK_CALL(process_helper(3, test3, 1));

    char* test4[3] = { "PN 0 0 0", "IF 0: PN 0 0 1", "PN 0 0" };
    CHECK_CALL(process_helper(3, test4, 0));

    char* test5[3] = { "X 0", "ELSE: X 1", "X" };
    CHECK_CALL(process_helper(3, test5, 0));

    char* test6[3] = { "X 0", "ELIF 1: X 1", "X" };
    CHECK_CALL(process_helper(3, test6, 0));

    char* test7[4] = { "X 0", "ELIF 1: X 1", "ELSE: X 1", "X" };
    CHECK_CALL(process_helper(4, test7, 0));

    char* test8[4] = { "X 0", "IF 0: X 2", "ELSE: X 1", "X" };
    CHECK_CALL(process_helper(4, test8, 1));

    char* test9[4] = { "X 0", "IF 0: X 2", "ELIF 1: X 1", "X" };
    CHECK_CALL(process_helper(4, test9, 1));

    char* test10[5] = { "X 0", "IF 1: X 1", "ELIF 1: X 2", "ELSE: X 3", "X" };
    CHECK_CALL(process_helper(5, test10, 1));

    char* test11[5] = { "X 0", "IF 0: X 1", "ELIF 1: X 2", "ELSE: X 3", "X" };
    CHECK_CALL(process_helper(5, test11, 2));

    char* test12[5] = { "X 0", "IF 0: X 1", "ELIF 0: X 2", "ELSE: X 3", "X" };
    CHECK_CALL(process_helper(5, test12, 3));

    PASS();
}

TEST test_FLIP() {
    char* test1[2] = { "FLIP 0", "FLIP" };
    CHECK_CALL(process_helper(2, test1, 0));

    char* test2[1] = { "FLIP" };
    CHECK_CALL(process_helper(1, test2, 0));

    char* test3[2] = { "FLIP", "FLIP" };
    CHECK_CALL(process_helper(2, test3, 1));

    char* test4[2] = { "FLIP 100", "FLIP" };
    CHECK_CALL(process_helper(2, test4, 1));

    PASS();
}

TEST test_L() {
    char* test1[3] = { "X 0", "L 1 10: X I", "X" };
    CHECK_CALL(process_helper(3, test1, 10));

    char* test2[3] = { "X 0", "L 1 -10: X I", "X" };
    CHECK_CALL(process_helper(3, test2, -10));

    char* test3[3] = { "X 0", "L 1 10: X ADD X I", "X" };
    CHECK_CALL(process_helper(3, test3, 55));

    PASS();
}

TEST test_O() {
    scene_state_t ss;
    ss_init(&ss);

    char* test1[6] = {
        "O.MIN 0", "O.MAX 63", "O.INC 1", "O.WRAP 1", "O 0", "O"
    };
    CHECK_CALL(process_helper_state(&ss, 6, test1, 0));

    char* test2[1] = { "O" };
    CHECK_CALL(process_helper_state(&ss, 1, test2, 1));

    char* test3[2] = { "O 0", "O" };
    CHECK_CALL(process_helper_state(&ss, 2, test3, 0));

    char* test4[2] = { "O 63", "O" };
    CHECK_CALL(process_helper_state(&ss, 2, test4, 63));

    char* test5[1] = { "O" };
    CHECK_CALL(process_helper_state(&ss, 1, test5, 0));

    char* test6[4] = { "O 0", "O.INC -1", "O", "O" };
    CHECK_CALL(process_helper_state(&ss, 4, test6, 63));

    char* test7[4] = { "O 0", "O.WRAP 0", "O", "O" };
    CHECK_CALL(process_helper_state(&ss, 4, test7, 0));

    PASS();
}

TEST test_P() {
    char* test1[2] = { "P 0 1", "P 0" };
    CHECK_CALL(process_helper(2, test1, 1));

    char* test2[2] = { "P 0 2", "P 0" };
    CHECK_CALL(process_helper(2, test2, 2));

    PASS();
}

TEST test_PN() {
    char* test1[2] = { "PN 0 0 1", "PN 0 0" };
    CHECK_CALL(process_helper(2, test1, 1));

    char* test2[2] = { "PN 0 0 2", "PN 0 0" };
    CHECK_CALL(process_helper(2, test2, 2));

    char* test3[3] = { "P.N 0", "P 0 3", "PN 0 0" };
    CHECK_CALL(process_helper(3, test3, 3));

    char* test4[3] = { "P.N 0", "PN 0 0 4", "P 0" };
    CHECK_CALL(process_helper(3, test4, 4));

    PASS();
}

TEST test_Q() {
    scene_state_t ss;
    ss_init(&ss);

    char* test1[2] = { "Q.N 16", "Q.N" };
    CHECK_CALL(process_helper_state(&ss, 2, test1, 16));

    for (int i = 1; i <= 16; i++) {
        char buf1[20];
        char buf2[20];
        sprintf(buf1, "Q.N %d", i);
        sprintf(buf2, "Q %d", i);
        char* test2[3] = { buf1, buf2, "Q" };
        CHECK_CALL(process_helper_state(&ss, 3, test2, 1));
    }

    for (int i = 1; i <= 16; i++) {
        char buf1[20];
        sprintf(buf1, "Q.N %d", i);
        char* test3[2] = { buf1, "Q" };
        CHECK_CALL(process_helper_state(&ss, 2, test3, 17 - i));
    }

    // 1+2+3+4+5+6+7+8+9+10+11+12+13+14+15+16 = 136
    // 136 / 16 = 8.5
    char* test4[1] = { "Q.AVG" };
    CHECK_CALL(process_helper_state(&ss, 1, test4, 9));

    char* test5[2] = { "Q.AVG 5", "Q.AVG" };
    CHECK_CALL(process_helper_state(&ss, 2, test5, 5));

    for (int i = 1; i <= 16; i++) {
        char* test6[1] = { "Q" };
        CHECK_CALL(process_helper_state(&ss, 1, test6, 5));
    }

    PASS();
}

TEST test_X() {
    char* test1[2] = { "X 0", "X" };
    CHECK_CALL(process_helper(2, test1, 0));

    char* test2[2] = { "X 10", "X" };
    CHECK_CALL(process_helper(2, test2, 10));

    char* test3[2] = { "X 10", "ADD 5 X" };
    CHECK_CALL(process_helper(2, test3, 15));

    PASS();
}

TEST test_sub_commands() {
    char* test1[2] = { "X 10; Y 20; Z 30", "ADD X ADD Y Z" };
    CHECK_CALL(process_helper(2, test1, 60));

    char* test2[2] = { "IF 1: X 1; Y 2; Z 3", "ADD X ADD Y Z" };
    CHECK_CALL(process_helper(2, test2, 6));

    char* test3[3] = { "X 0; Y 0; Z 0", "IF 0: X 1; Y 2; Z 3",
                       "ADD X ADD Y Z" };
    CHECK_CALL(process_helper(3, test3, 0));

    PASS();
}

TEST test_blank_command() {
    scene_state_t ss;
    ss_init(&ss);
    exec_state_t es;
    es_init(&es);
    es_push(&es);
    es_variables(&es)->script_number = 0;
    tele_command_t cmd;
    char error_msg[TELE_ERROR_MSG_LENGTH];

    char* test = "";
    error_t error = parse(test, &cmd, error_msg);
    if (error != E_OK) { FAIL(); }
    if (validate(&cmd, error_msg) != E_OK) { FAIL(); }

    process_result_t result = process_command(&ss, &es, &cmd);

    ASSERT_EQ(result.has_value, false);
    ASSERT_EQ(result.value, 0);

    PASS();
}

TEST test_P_ROT_1() {
    scene_state_t ss;
    ss_init(&ss);

    char* prep1[3] = { "P.START 0", "P.END 3", "0" };
    CHECK_CALL(process_helper_state(&ss, 3, prep1, 0));

    char* prep2[5] = { "P 0 1", "P 1 2", "P 2 3", "P 3 4", "P P.END" };
    CHECK_CALL(process_helper_state(&ss, 5, prep2, 4));

    char* prep3[2] = { "L 4 63: P I -1", "P + 1 P.END" };
    CHECK_CALL(process_helper_state(&ss, 2, prep3, -1));

    char* test1[2] = { "P.ROT 1", "P 0" };
    CHECK_CALL(process_helper_state(&ss, 2, test1, 4));

    char* test2[1] = { "P 1" };
    CHECK_CALL(process_helper_state(&ss, 1, test2, 1));

    char* test3[1] = { "P 2" };
    CHECK_CALL(process_helper_state(&ss, 1, test3, 2));

    char* test4[1] = { "P 3" };
    CHECK_CALL(process_helper_state(&ss, 1, test4, 3));

    PASS();
}

TEST test_P_ROT_3() {
    scene_state_t ss;
    ss_init(&ss);

    char* prep1[3] = { "P.START 0", "P.END 3", "0" };
    CHECK_CALL(process_helper_state(&ss, 3, prep1, 0));

    char* prep2[5] = { "P 0 1", "P 1 2", "P 2 3", "P 3 4", "P P.END" };
    CHECK_CALL(process_helper_state(&ss, 5, prep2, 4));

    char* prep3[2] = { "L 4 63: P I -1", "P + 1 P.END" };
    CHECK_CALL(process_helper_state(&ss, 2, prep3, -1));

    char* test1[2] = { "P.ROT 3", "P 0" };
    CHECK_CALL(process_helper_state(&ss, 2, test1, 2));

    char* test2[1] = { "P 1" };
    CHECK_CALL(process_helper_state(&ss, 1, test2, 3));

    char* test3[1] = { "P 2" };
    CHECK_CALL(process_helper_state(&ss, 1, test3, 4));

    char* test4[1] = { "P 3" };
    CHECK_CALL(process_helper_state(&ss, 1, test4, 1));

    PASS();
}

TEST test_phz_selection() {
    scene_state_t ss;
    ss_init(&ss);
    char* initial[]={"PHZ"};
    CHECK_CALL(process_helper_state(&ss,1,initial,1));
    char* addr[]={"PHZ.ADDR"};
    CHECK_CALL(process_helper_state(&ss,1,addr,69));
    for (unsigned i=0;i<4;++i) {
        ASSERT_EQ(ss.phz.address[i],i ? 0 : 69);
        ASSERT_EQ(ss.phz.channel[i],1);
    }
    char* edit[]={"PHZ 2","PHZ.ADDR 70","PHZ.ADDR"};
    CHECK_CALL(process_helper_state(&ss,3,edit,70));
    char* scope[]={"PHZ4: X PHZ","X"};
    ss.phz.output_valid=1;
    CHECK_CALL(process_helper_state(&ss,2,scope,4));
    ASSERT_EQ(ss.phz.selected,1);
    ASSERT_EQ(ss.phz.output_valid,0);
    char* rejected[]={"PHZ3: PHZ.ADDR 120","PHZ"};
    CHECK_CALL(process_helper_state(&ss,2,rejected,2));
    ASSERT_EQ(ss.phz.address[2],0);
    char* reset_scope[]={"PHZ4: INIT.SCENE","PHZ"};
    CHECK_CALL(process_helper_state(&ss,2,reset_scope,1));
    ASSERT_EQ(ss.phz.scope_active,0);
    CHECK_CALL(process_helper_state(&ss,3,edit,70));
    char* invalid[]={"PHZ 0","PHZ 5","PHZ -1","PHZ.ADDR -1",
                     "PHZ.ADDR 7","PHZ.ADDR 120","PHZ.ADDR"};
    CHECK_CALL(process_helper_state(&ss,7,invalid,70));
    char* disabled[]={"PHZ.ADDR 0","PHZ.ADDR"};
    CHECK_CALL(process_helper_state(&ss,2,disabled,0));
    char* limits[]={"PHZ.ADDR 8","PHZ.ADDR 119","PHZ.ADDR"};
    CHECK_CALL(process_helper_state(&ss,3,limits,119));
    ss.phz.output_valid=1;
    ss_phz_init(&ss);
    ASSERT_EQ(ss.phz.selected,0);
    ASSERT_EQ(ss.phz.output_valid,0);
    for (unsigned i=0;i<4;++i) {
        ASSERT_EQ(ss.phz.address[i],i ? 0 : 69);
        ASSERT_EQ(ss.phz.channel[i],1);
    }
    // The normal parser must reject a malformed scoped command before running it.
    tele_command_t command; char error_msg[TELE_ERROR_MSG_LENGTH];
    ASSERT_EQ(parse("PHZ4: NOTANOP",&command,error_msg),E_PARSE);
    ASSERT_EQ(ss.phz.selected,0);
    PASS();
}

TEST test_phz_signals() {
    scene_state_t ss; ss_init(&ss); phz_bus_reset();
    char* input[]={"PHZ.Q.IN 32 -1536","PHZ.Q.OK"};
    CHECK_CALL(process_helper_state(&ss,2,input,0));
    ASSERT_EQ(phz_bus.calls,2);
    ASSERT_EQ(phz_bus.last.mapping,32);
    ASSERT_EQ(phz_bus.last.value,0xfa00);
    ASSERT_EQ(phz_bus.last.op,1);
    ASSERT_EQ(ss.phz.remote_status,PC_ADMITTED);
    char* pos[]={"PHZ.Q.POS 1 8192","PHZ.Q.OK"};
    CHECK_CALL(process_helper_state(&ss,2,pos,0));
    ASSERT_EQ(phz_bus.last.value,8192);
    ASSERT_EQ(phz_bus.last.op,2);
    char* bad[]={"PHZ.Q.POS 1 -1","PHZ.Q.POS 1 16385","PHZ.Q.IN 33 5",
                 "PHZ.Q.IN 0 5","PHZ.Q.IN -255 5","PHZ.Q.OK"};
    CHECK_CALL(process_helper_state(&ss,6,bad,0));
    ASSERT_EQ(phz_bus.calls,4);
    ASSERT_EQ(ss.phz.error,PCS_ARGUMENT);
    char* read[]={"PHZ.Q.OUT 1"};
    char* ok[]={"PHZ.Q.OK"};
    phz_bus_reset();
    CHECK_CALL(process_helper_state(&ss,1,read,0));
    CHECK_CALL(process_helper_state(&ss,1,ok,1));
    ASSERT_EQ(phz_bus.calls,4); // OK is cached, not a bus transaction.
    phz_bus.voltage=0x8000;
    CHECK_CALL(process_helper_state(&ss,1,read,-32768));
    phz_bus.voltage=32767;
    CHECK_CALL(process_helper_state(&ss,1,read,32767));
    for(unsigned step=1;step<=4;++step) {
        phz_bus_reset(); phz_bus.fail_at=step; ss.phz.output_valid=1;
        CHECK_CALL(process_helper_state(&ss,1,read,0));
        CHECK_CALL(process_helper_state(&ss,1,ok,0));
        ASSERT_EQ(phz_bus.calls,step);
        ASSERT_EQ(ss.phz.driver_error,-8);
    }
    for(unsigned step=2;step<=4;step+=2) {
        phz_bus_reset(); phz_bus.corrupt_at=step;
        CHECK_CALL(process_helper_state(&ss,1,read,0));
        ASSERT_EQ(ss.phz.output_valid,0);
        ASSERT_EQ(ss.phz.error,PCS_PROTOCOL);
        ASSERT_EQ(phz_bus.calls,step);
    }
    phz_bus_reset(); phz_bus.available=0;
    CHECK_CALL(process_helper_state(&ss,1,read,0));
    CHECK_CALL(process_helper_state(&ss,1,ok,0));
    ASSERT_EQ(ss.phz.error,PCS_UNAVAILABLE);
    phz_bus_reset(); phz_bus.disposition=PC_WRONG_APP;
    CHECK_CALL(process_helper_state(&ss,1,read,0));
    ASSERT_EQ(phz_bus.calls,2);
    ASSERT_EQ(ss.phz.remote_status,PC_WRONG_APP);
    phz_bus_reset();
    char* disabled[]={"PHZ 2","PHZ.Q.OUT 1"};
    CHECK_CALL(process_helper_state(&ss,2,disabled,0));
    ASSERT_EQ(phz_bus.calls,0);
    ASSERT_EQ(ss.phz.error,PCS_DISABLED);
    char* scoped[]={"PHZ4: PHZ.ADDR 70","PHZ4: X PHZ.Q.OUT 32","PHZ.Q.OK"};
    CHECK_CALL(process_helper_state(&ss,3,scoped,0));
    ASSERT_EQ(phz_bus.address,70);
    ASSERT_EQ(phz_bus.last.mapping,32);
    ASSERT_EQ(ss.phz.selected,1);
    ss.phz.tag[1]=UINT16_MAX;
    char* wrap[]={"PHZ.ADDR 69","PHZ.Q.OUT 1"};
    CHECK_CALL(process_helper_state(&ss,2,wrap,0));
    ASSERT_EQ(phz_bus.last.tag,1);
    char* invalid_read[]={"PHZ.Q.OUT 257"};
    unsigned calls=phz_bus.calls;
    CHECK_CALL(process_helper_state(&ss,1,invalid_read,0));
    ASSERT_EQ(phz_bus.calls,calls);
    CHECK_CALL(process_helper_state(&ss,1,ok,0));
    PASS();
}

TEST test_phz_errors() {
    scene_state_t ss; ss_init(&ss); phz_bus_reset();
    char* err[]={"PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,1,err,PHZ_ERR_NONE));
    char* bad[]={"PHZ.Q.POS 1 -1","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,bad,PHZ_ERR_ARGUMENT));
    char* inspect[]={"X PHZ","Y PHZ.ADDR","Z PHZ.Q.OK","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,4,inspect,PHZ_ERR_ARGUMENT));
    CHECK_CALL(process_helper_state(&ss,1,err,PHZ_ERR_ARGUMENT));
    ASSERT_EQ(phz_bus.calls,0);
    char* good[]={"PHZ.Q.IN 1 0","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,good,PHZ_ERR_NONE));
    ASSERT_EQ(ss.phz.remote_status,PC_ADMITTED); // not applied
    const int expected[]={PHZ_ERR_ARGUMENT,PHZ_ERR_WRONG_APP,PHZ_ERR_UNAVAILABLE,
                          PHZ_ERR_BUSY,PHZ_ERR_QUEUE_FULL,PHZ_ERR_UNSUPPORTED};
    for(unsigned i=0;i<6;++i) {
        phz_bus_reset(); phz_bus.disposition=(uint8_t)(i+2);
        CHECK_CALL(process_helper_state(&ss,2,good,expected[i]));
        ASSERT_EQ(phz_bus.calls,2);
    }
    for(unsigned step=1;step<=4;++step) {
        phz_bus_reset(); phz_bus.fail_at=step;
        char* read[]={"X PHZ.Q.OUT 1","PHZ.ERR"};
        CHECK_CALL(process_helper_state(&ss,2,read,step%2 ? PHZ_ERR_SEND : PHZ_ERR_RECEIVE));
        ASSERT_EQ(phz_bus.calls,step);
    }
    phz_bus_reset(); phz_bus.corrupt_at=2;
    CHECK_CALL(process_helper_state(&ss,2,good,PHZ_ERR_PROTOCOL));
    phz_bus_reset(); phz_bus.available=0;
    char* unavailable[]={"X PHZ.Q.OUT 1","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,unavailable,PHZ_ERR_UNAVAILABLE));
    char* scope[]={"PHZ4: PHZ.Q.IN 1 0","PHZ.ERR"};
    phz_bus_reset();
    CHECK_CALL(process_helper_state(&ss,2,scope,PHZ_ERR_DISABLED));
    ASSERT_EQ(ss.phz.selected,0);
    ASSERT_EQ(phz_bus.calls,0);
    char* invalid_slot[]={"PHZ 5","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,invalid_slot,PHZ_ERR_ARGUMENT));
    ASSERT_EQ(ss.phz.selected,0);
    char* valid_slot[]={"PHZ 1","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,valid_slot,PHZ_ERR_NONE));
    char* invalid_addr[]={"PHZ.ADDR 128","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,invalid_addr,PHZ_ERR_ARGUMENT));
    ASSERT_EQ(ss.phz.address[0],69);
    char* valid_addr[]={"PHZ.ADDR 70","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,valid_addr,PHZ_ERR_NONE));
    CHECK_CALL(process_helper_state(&ss,2,bad,PHZ_ERR_ARGUMENT));
    ss_phz_init(&ss);
    CHECK_CALL(process_helper_state(&ss,1,err,PHZ_ERR_NONE));
    PASS();
}

TEST test_phz_quantizer() {
    scene_state_t ss; ss_init(&ss); phz_bus_reset();
    char* scale[]={"PHZ.QTZ.SCALE 8 15","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,scale,PHZ_ERR_NONE));
    ASSERT(phz_bus.is_quantizer);
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_SCALE);
    ASSERT_EQ(phz_bus.last_qtz.slot,8);
    ASSERT_EQ(phz_bus.last_qtz.scale,15);
    ASSERT_EQ(phz_bus.last_length,9);

    phz_bus_reset();
    char* root[]={"PHZ.QTZ.ROOT 1 11","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,root,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_ROOT);
    ASSERT_EQ(phz_bus.last_qtz.root,11);

    phz_bus_reset();
    char* octave[]={"PHZ.QTZ.OCT 2 -8","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,octave,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_OCTAVE);
    ASSERT_EQ(phz_bus.last_qtz.octave,-8);

    phz_bus_reset();
    char* mask[]={"PHZ.QTZ.MASK 3 -1","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,mask,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_MASK);
    ASSERT_EQ(phz_bus.last_qtz.mask,UINT16_MAX);

    phz_bus_reset();
    char* range[]={"PHZ.QTZ.RANGE 4 -3 4","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,range,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_RANGE);
    ASSERT_EQ(phz_bus.last_qtz.low,-3);
    ASSERT_EQ(phz_bus.last_qtz.high,4);

    phz_bus_reset();
    char* off[]={"PHZ.QTZ.ROFF 5","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,off,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_RANGE_OFF);
    ASSERT_EQ(phz_bus.last_length,7);

    phz_bus_reset();
    char* replace[]={"PHZ.QTZ.REPL 8 15 11 -8 -23206 1 -3 4","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,replace,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_REPLACE);
    ASSERT_EQ(phz_bus.last_qtz.slot,8);
    ASSERT_EQ(phz_bus.last_qtz.scale,15);
    ASSERT_EQ(phz_bus.last_qtz.root,11);
    ASSERT_EQ(phz_bus.last_qtz.octave,-8);
    ASSERT_EQ(phz_bus.last_qtz.mask,0xa55a);
    ASSERT_EQ(phz_bus.last_qtz.range_enabled,1);
    ASSERT_EQ(phz_bus.last_qtz.low,-3);
    ASSERT_EQ(phz_bus.last_qtz.high,4);
    ASSERT_EQ(phz_bus.last_length,16);

    const uint8_t golden[]={0x50,0x01,0x05,0x07,0x12,0x34,0x08,0x00,
                            0x0f,0x0b,0xf8,0xa5,0x5a,0x01,0xfd,0x04};
    uint8_t encoded[16]; size_t written=0;
    pc_qtz_request request=phz_bus.last_qtz; request.tag=0x1234;
    ASSERT(pc_qtz_encode(&request,encoded,sizeof encoded,&written));
    ASSERT_EQ(written,sizeof golden);
    ASSERT_EQ(memcmp(encoded,golden,sizeof golden),0);

    const char* invalid[]={"PHZ.QTZ.SCALE 1 -1","PHZ.QTZ.ROOT 1 12",
        "PHZ.QTZ.OCT 1 9","PHZ.QTZ.MASK 1 0","PHZ.QTZ.RANGE 1 4 -3",
        "PHZ.QTZ.ROFF 9","PHZ.QTZ.REPL 1 0 0 0 1 0 0 0"};
    for (size_t i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        unsigned calls=phz_bus.calls;
        char* lines[]={(char*)invalid[i],"PHZ.ERR"};
        CHECK_CALL(process_helper_state(&ss,2,lines,PHZ_ERR_ARGUMENT));
        ASSERT_EQ(phz_bus.calls,calls);
    }

    const int remote_errors[]={PHZ_ERR_ARGUMENT,PHZ_ERR_WRONG_APP,
        PHZ_ERR_UNAVAILABLE,PHZ_ERR_BUSY,PHZ_ERR_QUEUE_FULL,
        PHZ_ERR_UNSUPPORTED};
    char* remote[]={"PHZ.QTZ.ROOT 1 0","PHZ.ERR"};
    for (unsigned i=0;i<sizeof remote_errors/sizeof remote_errors[0];++i) {
        phz_bus_reset(); phz_bus.disposition=(uint8_t)(i+2);
        CHECK_CALL(process_helper_state(&ss,2,remote,remote_errors[i]));
        ASSERT_EQ(phz_bus.calls,2);
    }
    for (unsigned step=1;step<=2;++step) {
        phz_bus_reset(); phz_bus.fail_at=step;
        CHECK_CALL(process_helper_state(
            &ss,2,remote,step==1 ? PHZ_ERR_SEND : PHZ_ERR_RECEIVE));
        ASSERT_EQ(phz_bus.calls,step);
    }
    phz_bus_reset(); phz_bus.corrupt_at=2;
    CHECK_CALL(process_helper_state(&ss,2,remote,PHZ_ERR_PROTOCOL));

    phz_bus_reset();
    char* scoped[]={"PHZ4: PHZ.QTZ.ROOT 1 0","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,scoped,PHZ_ERR_DISABLED));
    ASSERT_EQ(phz_bus.calls,0);
    ASSERT_EQ(ss.phz.selected,0);

    phz_bus_reset();
    char* capture[]={"PHZ.QTZ.CAP 3 1","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,capture,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last_qtz.op,PC_QTZ_CAPTURE_CHORD);
    ASSERT_EQ(phz_bus.last_qtz.slot,3);
    ASSERT_EQ(phz_bus.last_qtz.root,1);
    ASSERT_EQ(phz_bus.capture_queries,2);
    ASSERT_EQ(phz_bus.calls,4);

    phz_bus_reset(); phz_bus.capture_result=PC_UNAVAILABLE;
    CHECK_CALL(process_helper_state(&ss,2,capture,PHZ_ERR_UNAVAILABLE));
    ASSERT_EQ(phz_bus.calls,4);

    phz_bus_reset(); phz_bus.capture_busy=1;
    CHECK_CALL(process_helper_state(&ss,2,capture,PHZ_ERR_BUSY));
    ASSERT_EQ(phz_bus.calls,4);

    const char* invalid_capture[]={"PHZ.QTZ.CAP 0 1",
                                   "PHZ.QTZ.CAP 1 0",
                                   "PHZ.QTZ.CAP 9 1",
                                   "PHZ.QTZ.CAP 1 9"};
    for (size_t i=0;i<sizeof invalid_capture/sizeof invalid_capture[0];++i) {
        phz_bus_reset();
        char* lines[]={(char*)invalid_capture[i],"PHZ.ERR"};
        CHECK_CALL(process_helper_state(&ss,2,lines,PHZ_ERR_ARGUMENT));
        ASSERT_EQ(phz_bus.calls,0);
    }
    PASS();
}

TEST test_phz_chord_bitmap() {
    scene_state_t ss; ss_init(&ss); phz_bus_reset();
    char* define[]={"PHZ.I2M.C.B 1 401","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,define,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last.family,PC_FAMILY_MIDI);
    ASSERT_EQ(phz_bus.last.op,PC_MIDI_CHORD_BITMAP);
    ASSERT_EQ(phz_bus.last.mapping,1);
    ASSERT_EQ(phz_bus.last.value,0x0191);
    ASSERT_EQ(phz_bus.last_length,9);

    phz_bus_reset();
    char* clear_all[]={"PHZ.I2M.C.B 0 0","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,clear_all,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last.mapping,0);
    ASSERT_EQ(phz_bus.last.value,0);

    const char* invalid[]={"PHZ.I2M.C.B -1 1","PHZ.I2M.C.B 9 1"};
    for (size_t i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        unsigned calls=phz_bus.calls;
        char* lines[]={(char*)invalid[i],"PHZ.ERR"};
        CHECK_CALL(process_helper_state(&ss,2,lines,PHZ_ERR_ARGUMENT));
        ASSERT_EQ(phz_bus.calls,calls);
    }

    phz_bus_reset(); phz_bus.disposition=PC_FULL;
    CHECK_CALL(process_helper_state(&ss,2,define,PHZ_ERR_QUEUE_FULL));
    phz_bus_reset();
    char* scoped[]={"PHZ4: PHZ.I2M.C.B 1 1","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,scoped,PHZ_ERR_DISABLED));
    ASSERT_EQ(phz_bus.calls,0);
    ASSERT_EQ(ss.phz.selected,0);

    phz_bus_reset();
    char* scale[]={"PHZ.I2M.C.SC 1 2","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,scale,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last.family,PC_FAMILY_MIDI);
    ASSERT_EQ(phz_bus.last.op,PC_MIDI_CHORD_SCALE);
    ASSERT_EQ(phz_bus.last.mapping,1);
    ASSERT_EQ(phz_bus.last.value,2);
    ASSERT_EQ(phz_bus.capture_queries,2);
    ASSERT_EQ(phz_bus.calls,4);

    phz_bus_reset(); phz_bus.capture_result=PC_UNAVAILABLE;
    CHECK_CALL(process_helper_state(&ss,2,scale,PHZ_ERR_UNAVAILABLE));
    ASSERT_EQ(phz_bus.calls,4);
    phz_bus_reset(); phz_bus.capture_busy=1;
    CHECK_CALL(process_helper_state(&ss,2,scale,PHZ_ERR_BUSY));
    ASSERT_EQ(phz_bus.calls,4);

    phz_bus_reset();
    char* transpose[]={"PHZ.I2M.C.TRP 1 -2","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,transpose,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last.op,PC_MIDI_CHORD_TRANSPOSE);
    ASSERT_EQ(phz_bus.last.mapping,1);
    ASSERT_EQ((int16_t)phz_bus.last.value,-2);
    ASSERT_EQ(phz_bus.calls,2);

    phz_bus_reset();
    char* play[]={"PHZ.I2M.C 1 60 100","PHZ.ERR"};
    CHECK_CALL(process_helper_state(&ss,2,play,PHZ_ERR_NONE));
    ASSERT_EQ(phz_bus.last.op,PC_MIDI_CHORD_PLAY);
    ASSERT_EQ(phz_bus.last.mapping,1);
    ASSERT_EQ(phz_bus.last.value,(60<<8)|100);
    ASSERT_EQ(phz_bus.calls,2);

    const char* invalid_transform[]={
        "PHZ.I2M.C.SC -1 1","PHZ.I2M.C.SC 1 9",
        "PHZ.I2M.C.TRP 9 0","PHZ.I2M.C.TRP 1 -128",
        "PHZ.I2M.C 0 60 100","PHZ.I2M.C 1 128 100",
        "PHZ.I2M.C 1 60 128"};
    for (size_t i=0;i<sizeof invalid_transform/sizeof invalid_transform[0];++i) {
        phz_bus_reset();
        char* lines[]={(char*)invalid_transform[i],"PHZ.ERR"};
        CHECK_CALL(process_helper_state(&ss,2,lines,PHZ_ERR_ARGUMENT));
        ASSERT_EQ(phz_bus.calls,0);
    }
    PASS();
}

SUITE(process_suite) {
    RUN_TEST(test_phz_chord_bitmap);
    RUN_TEST(test_phz_quantizer);
    RUN_TEST(test_phz_errors);
    RUN_TEST(test_phz_signals);
    RUN_TEST(test_phz_selection);
    RUN_TEST(test_numbers);
    RUN_TEST(test_ADD);
    RUN_TEST(test_PROB);
    RUN_TEST(test_IF);
    RUN_TEST(test_FLIP);
    RUN_TEST(test_L);
    RUN_TEST(test_O);
    RUN_TEST(test_P);
    RUN_TEST(test_Q);
    RUN_TEST(test_PN);
    RUN_TEST(test_X);
    RUN_TEST(test_sub_commands);
    RUN_TEST(test_blank_command);
    RUN_TEST(test_P_ROT_1);
    RUN_TEST(test_P_ROT_3);
}
