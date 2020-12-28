// (c) 2020 shdown
// This code is licensed under MIT license (see LICENSE.MIT for details)

#include "dasm.h"

void dasm(const Instr *code, size_t ncode, FILE *f)
{
    static const char *names[256] = {
        [OP_LOAD_CONST] = "OP_LOAD_CONST",
        [OP_LOAD_LOCAL] = "OP_LOAD_LOCAL",
        [OP_LOAD_AT] = "OP_LOAD_AT",
        [OP_LOAD_GLOBAL] = "OP_LOAD_GLOBAL",

        [OP_MODIFY_LOCAL] = "OP_MODIFY_LOCAL",
        [OP_MODIFY_AT] = "OP_MODIFY_AT",
        [OP_MODIFY_GLOBAL] = "OP_MODIFY_GLOBAL",

        [OP_STORE_LOCAL] = "OP_STORE_LOCAL",
        [OP_STORE_AT] = "OP_STORE_AT",
        [OP_STORE_GLOBAL] = "OP_STORE_GLOBAL",

        [OP_PRINT] = "OP_PRINT",
        [OP_RETURN] = "OP_RETURN",

        [OP_JUMP] = "OP_JUMP",
        [OP_JUMP_UNLESS] = "OP_JUMP_UNLESS",
        [OP_CALL] = "OP_CALL",
        [OP_FUNCTION] = "OP_FUNCTION",

        [OP_NEG] = "OP_NEG",
        [OP_NOT] = "OP_NOT",

        [OP_AOP] = "OP_AOP",
        [OP_CMP_2WAY] = "OP_CMP_2WAY",
        [OP_CMP_3WAY] = "OP_CMP_3WAY",

        [OP_LIST] = "OP_LIST",
        [OP_DICT] = "OP_DICT",
        [OP_LEN] = "OP_LEN",

        [OP_LOAD_SYMBOLIC] = "OP_LOAD_SYMBOLIC",
        [OP_STORE_SYMBOLIC] = "OP_STORE_SYMBOLIC",
        [OP_MODIFY_SYMBOLIC] = "OP_MODIFY_SYMBOLIC",
    };

    for (size_t i = 0; i < ncode; ++i) {
        Instr instr = code[i];
        const char *name = names[instr.opcode];
        if (!name)
            name = "?";
        fprintf(f, "%8zu | %20s  %d, %" PRIi32 "\n", i, name, (int) instr.a, (int32_t) instr.c);
    }
}
