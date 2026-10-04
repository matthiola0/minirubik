/* Freestanding C reference for the RV32I solver: the search of solver_ida.c
 * with the tables of build/tables.h, run on Ripes with no C library.
 *
 * rv32/rv32.py build compiles it with
 *   riscv64-unknown-elf-gcc -O2 -march=rv32i -mabi=ilp32 -ffreestanding
 *     -nostdlib -nostartfiles -Wl,--no-relax -Ibuild solver_ref.c
 * and rv32.py run replaces the 14 characters of `input` in the ELF.
 *
 * Output: the moves separated by spaces, then a newline, through Ripes
 * ecall 11. Exit code through ecall 93: 0 solved and verified, 1 search or
 * replay failed, 2 invalid input.
 */
#include <stdint.h>

#include "tables.h"

enum { CUBIES = 7, MAX_DEPTH = 11 };

static const char input[] = "21345671111111";

static const char move_names[9][3] = {"R", "R2", "R'", "B", "B2",
                                      "B'", "D", "D2", "D'"};
/* Each destination takes a cubie from source[face][destination]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;
typedef struct {
    uint16_t p, o, next_p, next_o;
    uint8_t face, turn, previous_face;
} frame_t;
typedef struct {
    uint8_t moves[MAX_DEPTH];
    unsigned length;
} solution_t;

#ifndef HOST_VERIFY
static void put_char(int c)
{
    register int a0 __asm__("a0") = c;
    register int a7 __asm__("a7") = 11;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void __attribute__((noreturn)) exit_with(int code)
{
    register int a0 __asm__("a0") = code;
    register int a7 __asm__("a7") = 93;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    for (;;)
        ;
}
#endif

/* Seven distinct cubie digits 1-7, seven twist digits 1-3, twist sum
 * divisible by 3, and nothing after the fourteenth character.
 */
static int parse_state(const char *text, state_t *state)
{
    unsigned seen = 0, sum = 0;
    for (unsigned i = 0; i < CUBIES; ++i) {
        unsigned digit = (unsigned) (text[i] - '1');
        if (digit >= CUBIES || (seen >> digit & 1U))
            return 0;
        seen |= 1U << digit;
        state->p[i] = (uint8_t) digit;
    }
    for (unsigned i = 0; i < CUBIES; ++i) {
        unsigned digit = (unsigned) (text[CUBIES + i] - '1');
        if (digit >= 3)
            return 0;
        state->o[i] = (uint8_t) digit;
        sum += digit;
    }
    while (sum >= 3)
        sum -= 3;
    return text[2 * CUBIES] == '\0' && sum == 0;
}

/* Ranking as sums of table entries, with no multiplication: the
 * permutation rank of solver_ida.c is sum(c_i * (6 - i)!), where c_i <= 6 - i
 * counts later cubies smaller than cubie i, and the orientation rank is
 * sum(o_i * 3^(5 - i)) over the first six twists. Entry [i][c] holds the
 * product for position i and digit c.
 */
static const uint16_t permutation_weight[CUBIES - 1][CUBIES] = {
    {0, 720, 1440, 2160, 2880, 3600, 4320},
    {0, 120, 240, 360, 480, 600},
    {0, 24, 48, 72, 96},
    {0, 6, 12, 18},
    {0, 2, 4},
    {0, 1},
};
static const uint16_t orientation_weight[CUBIES - 1][3] = {
    {0, 243, 486}, {0, 81, 162}, {0, 27, 54},
    {0, 9, 18},    {0, 3, 6},    {0, 1, 2},
};

/* The two coordinates of rank_state() in solver_ida.c, kept apart. The
 * last cubie has no smaller cubie after it and the last twist is implied.
 */
static void rank_coordinates(const state_t *state, uint16_t *p_rank,
                             uint16_t *o_rank)
{
    unsigned p = 0, o = 0;
    for (unsigned i = 0; i < CUBIES - 1; ++i) {
        unsigned smaller = 0;
        for (unsigned j = i + 1; j < CUBIES; ++j)
            smaller += state->p[j] < state->p[i];
        p += permutation_weight[i][smaller];
        o += orientation_weight[i][state->o[i]];
    }
    *p_rank = (uint16_t) p;
    *o_rank = (uint16_t) o;
}

static unsigned heuristic(uint16_t p, uint16_t o)
{
    unsigned hp = permutation_distance[p], ho = orientation_distance[o];
    return hp > ho ? hp : ho;
}

/* solve() from solver_ida.c with the permutation distance checked first. */
static int solve(uint16_t p, uint16_t o, solution_t *solution)
{
    frame_t stack[MAX_DEPTH + 1];
    solution->length = 0;
    if ((p | o) == 0)
        return 1;
    for (unsigned bound = heuristic(p, o); bound <= MAX_DEPTH; ++bound) {
        unsigned depth = 0;
        unsigned remaining = bound - 1;
        uint16_t here_p = p, here_o = o, next_p, next_o;
        unsigned face = 0, turn, previous_face = UINT8_MAX;
        const uint16_t *p_row, *o_row;

    next_face:
        if (face == previous_face)
            ++face;
        if (face == 3)
            goto backtrack;
        p_row = permutation[face];
        o_row = orientation[face];
        next_p = here_p;
        next_o = here_o;
        turn = 0;

    next_turn:
        if (turn == 3) {
            ++face;
            goto next_face;
        }
        next_p = p_row[next_p];
        next_o = o_row[next_o];
        ++turn;
        if (permutation_distance[next_p] > remaining)
            goto next_turn;
        if (orientation_distance[next_o] > remaining)
            goto next_turn;
        solution->moves[depth] = (uint8_t) (face * 3U + turn - 1U);
        if ((next_p | next_o) == 0) {
            solution->length = depth + 1;
            return 1;
        }
        stack[depth] = (frame_t) {here_p, here_o, next_p, next_o,
                                 (uint8_t) face, (uint8_t) turn,
                                 (uint8_t) previous_face};
        ++depth;
        --remaining;
        here_p = next_p;
        here_o = next_o;
        previous_face = face;
        face = 0;
        goto next_face;

    backtrack:
        if (depth == 0)
            continue;
        --depth;
        ++remaining;
        frame_t *frame = &stack[depth];
        here_p = frame->p;
        here_o = frame->o;
        next_p = frame->next_p;
        next_o = frame->next_o;
        face = frame->face;
        turn = frame->turn;
        previous_face = frame->previous_face;
        p_row = permutation[face];
        o_row = orientation[face];
        goto next_turn;
    }
    return 0;
}

/* T5: apply the path to the cubie arrays and check that it solves them. */
static int replay_solves(state_t state, const solution_t *solution)
{
    for (unsigned i = 0; i < solution->length; ++i) {
        unsigned face = 0, turns = solution->moves[i];
        while (turns >= 3) {
            turns -= 3;
            ++face;
        }
        for (unsigned t = 0; t <= turns; ++t) {
            state_t next;
            for (unsigned k = 0; k < CUBIES; ++k) {
                unsigned from = source[face][k];
                unsigned sum = state.o[from] + twist[face][k];
                next.p[k] = state.p[from];
                next.o[k] = (uint8_t) (sum >= 3 ? sum - 3 : sum);
            }
            state = next;
        }
    }
    for (unsigned k = 0; k < CUBIES; ++k)
        if (state.p[k] != k || state.o[k] != 0)
            return 0;
    return 1;
}

#ifndef HOST_VERIFY
void __attribute__((noreturn)) _start(void)
{
    const char *text = input;
    state_t state;
    uint16_t p, o;
    solution_t solution;
    /* Hide the address so the compiler cannot fold the parse and rank. */
    __asm__("" : "+r"(text));
    if (!parse_state(text, &state))
        exit_with(2);
    rank_coordinates(&state, &p, &o);
    if (!solve(p, o, &solution) || !replay_solves(state, &solution))
        exit_with(1);
    for (unsigned i = 0; i < solution.length; ++i) {
        if (i)
            put_char(' ');
        for (const char *c = move_names[solution.moves[i]]; *c; ++c)
            put_char(*c);
    }
    put_char('\n');
    exit_with(0);
}
#endif
