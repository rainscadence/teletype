#include "match_token_tests.h"

#include <string.h>

#include "greatest/greatest.h"
#include "match_token.h"
#include "ops/op.h"
#include "ops/op_enum.h"

// This test asserts that match_token always returns the correct op, it does
// this by starting with the op in question, extracting the name and running
// that through match_token.
TEST match_token_should_return_op() {
    for (size_t i = 0; i < E_OP__LENGTH; i++) {
        const tele_op_t* op = tele_ops[i];
        const char* text = op->name;
        tele_data_t data;
        bool result = match_token(text, strlen(text), &data);
        ASSERT_EQm(text, result, true);
        ASSERT_EQm(text, data.tag, OP);
        ASSERT_EQm(text, data.value, (int16_t)i);
    }
    PASS();
}

// As match_token_should_return_op, but for mods.
TEST match_token_should_return_mod() {
    for (size_t i = 0; i < E_MOD__LENGTH; i++) {
        const tele_mod_t* mod = tele_mods[i];
        const char* text = mod->name;
        tele_data_t data;
        bool result = match_token(text, strlen(text), &data);
        ASSERT_EQm(text, result, true);
        ASSERT_EQm(text, data.tag, MOD);
        ASSERT_EQm(text, data.value, (int16_t)i);
    }
    PASS();
}

TEST phz_tokens_require_exact_matches() {
    const char *bad[] = {"PH", "PHZ.", "PHZ0", "PHZ5", "PHZ10", "PHZ1X",
        "PHZ.Q", "PHZ.Q.I", "PHZ.Q.INX", "PHZ.Q.IN PHZ", "PHZPHZ", "phz",
        "PHZ.QTZ", "PHZ.QTZ.", "PHZ.QTZ.CAPX", "PHZ.QTZ.REPLACE",
        "PHZ.QTZ.SCALEX", "PHZ.I2M", "PHZ.I2M.", "PHZ.I2M.CX",
        "PHZ.I2M.C.BX", "PHZ.I2M.C.SCX", "PHZ.I2M.C.TRPX"};
    tele_data_t data;
    for (size_t i=0; i<sizeof(bad)/sizeof(bad[0]); ++i)
        ASSERT_FALSEm(bad[i], match_token(bad[i],strlen(bad[i]),&data));
    const char bounded[] = {'P','H','Z','1','X'};
    ASSERT(match_token(bounded,4,&data));
    ASSERT_EQ(data.tag,MOD);
    ASSERT_EQ(data.value,E_MOD_PHZ1);
    ASSERT_FALSE(match_token(bounded,5,&data));
    const char nul[] = {'P','H','Z',0,'X'};
    ASSERT_FALSE(match_token(nul,sizeof nul,&data));
    PASS();
}

SUITE(match_token_suite) {
    RUN_TEST(match_token_should_return_op);
    RUN_TEST(match_token_should_return_mod);
    RUN_TEST(phz_tokens_require_exact_matches);
}
