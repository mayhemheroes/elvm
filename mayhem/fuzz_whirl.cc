// In-process libFuzzer harness for the Whirl interpreter (tools/whirl.cc).
//
// The upstream `whirl` target is a raw file-input CLI: `whirl program.wrl` reads a file of
// '0'/'1' bytes and interprets it. To get useful in-process edge coverage under Mayhem (and to
// exercise the exact interpreter code — op_ring::execute / math_ring::execute and the main
// dispatch loop — where the real defects live: div-by-zero, signed-overflow negate, unbounded
// memory growth), we drive the same code path directly instead of the file-reading main().
//
// This file is ADDITIVE: tools/whirl.cc is included unmodified. Its main() is renamed away so it
// does not collide with the fuzzing driver, and every file-scope global it defines is reset at the
// start of each iteration so runs are independent.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#define main whirl_cli_main_UNUSED
#include "../tools/whirl.cc"
#undef main

// The interpreter's IntIO/AscIO ops printf to stdout; that output is harmless (Mayhem captures the
// target's stdout) so it is left as-is — we deliberately avoid redirecting to any absolute path,
// since the commit image is mounted read-only during coverage collection.

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Reset all interpreter globals so each input runs from a clean machine state.
    memory.clear();
    program.clear();
    ops = op_ring();
    math = math_ring();
    cur = &ops;
    next_instruction = true;

    // Load the program exactly as whirl.cc's main() does: only '0'/'1' bytes are significant.
    program.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '1')
            program.push_back(true);
        else if (data[i] == '0')
            program.push_back(false);
    }

    memory.push_back(0);
    mem_pos = memory.begin();
    prog_pos = program.begin();

    // Bound the interpreter: a fuzz input can encode an infinite jump loop (PAdd/If), which the
    // real binary would hang on too (Mayhem times it out). A step cap keeps the harness returning
    // so the fuzzer keeps making progress; it does not alter the interpreter's semantics.
    bool execute = false;
    unsigned long steps = 0;
    const unsigned long STEP_LIMIT = 2000000UL;
    while (prog_pos != program.end()) {
        if (++steps > STEP_LIMIT)
            break;
        next_instruction = true;

        if (*prog_pos) {
            cur->rotate();
            execute = false;
        } else {
            cur->switch_dir();
            if (execute) {
                if (!cur->execute())
                    return 0;
                if (cur == &ops)
                    cur = &math;
                else
                    cur = &ops;
                execute = false;
            } else {
                execute = true;
            }
        }

        if (next_instruction)
            ++prog_pos;
    }

    return 0;
}
