/* Node counts of IDA* variants over every state at one distance.
 *
 * Usage: ida_bench [--depth D] [--subset CUBIES] [--csv FILE] VARIANT...
 *
 *   max          the search in solver_ida.c: max of the permutation and
 *                orientation distances, same-face pruning
 *   max-noprune  max without same-face pruning
 *   perm         permutation distance only
 *   orient       orientation distance only
 *   os           max of the permutation distance and a distance over the
 *                orientation together with the positions of the cubies
 *                named by --subset (1 to 4 digits 1-7, default 123)
 *
 * Every variant is checked for admissibility over all 3,674,160 states and
 * every returned path must solve the cube at the exact distance. The run
 * for max is also compared move by move with solve() from solver_ida.c,
 * outside the timed loop.
 *
 * Output columns: target_bytes and target_bytes4 are the static tables a
 * target build would need, with subset positions ranked densely and the
 * distances in bytes or packed in 4 bits; this program itself uses larger
 * sparse host tables. worst_state is the state with the most candidates.
 * seconds is host time for the search loop, including path checks and CSV
 * output; clock() is wall time on Windows and CPU time on Linux.
 */
#define main solver_ida_main
#include "../solver_ida.c"
#undef main

#include <time.h>

enum { MAX_SUBSET = 4, SUBSET_CODES = 7 * 7 * 7 * 7 };

typedef struct {
    const char *name;
    uint8_t prune, use_p, use_o, use_s;
} variant_t;

static const variant_t variants[] = {
    {"max", 1, 1, 1, 0},  {"max-noprune", 0, 1, 1, 0},
    {"perm", 1, 1, 0, 0}, {"orient", 1, 0, 1, 0},
    {"os", 1, 1, 0, 1},
};
enum { VARIANTS = sizeof variants / sizeof *variants };

/* Positions of the subset cubies, one base-7 digit each, first cubie most
 * significant. Codes with repeated digits are never reached.
 */
static uint8_t subset[MAX_SUBSET], subset_size;
static uint16_t subset_move[3][SUBSET_CODES];
static uint8_t subset_distance[ORIENTATIONS * SUBSET_CODES];

typedef struct {
    uint64_t expanded, candidates;
    unsigned iterations;
} counts_t;

typedef struct {
    uint16_t p, o, s, next_p, next_o, next_s;
    uint8_t face, turn, previous_face;
} bench_frame_t;

static unsigned arrangements(unsigned k)
{
    unsigned count = 1;
    for (unsigned i = 0; i < k; ++i)
        count *= CUBIES - i;
    return count;
}

static uint16_t subset_code(const uint8_t position[MAX_SUBSET])
{
    uint16_t code = 0;
    for (uint8_t i = 0; i < subset_size; ++i)
        code = (uint16_t) (code * 7U + position[i]);
    return code;
}

static uint16_t state_subset_code(const state_t *state)
{
    uint8_t position[MAX_SUBSET] = {0};
    for (uint8_t i = 0; i < subset_size; ++i)
        for (uint8_t j = 0; j < CUBIES; ++j)
            if (state->p[j] == subset[i])
                position[i] = j;
    return subset_code(position);
}

/* BFS over (orientation, subset positions) from the solved state, where
 * cubie c sits at position c. Returns the number of pairs reached.
 */
static uint32_t build_subset(unsigned *maximum)
{
    static uint32_t queue[ORIENTATIONS * SUBSET_CODES];
    uint8_t destination[3][CUBIES];
    uint32_t head = 0, tail = 1;
    unsigned codes = 1;
    for (uint8_t i = 0; i < subset_size; ++i)
        codes *= 7;
    for (uint8_t face = 0; face < 3; ++face)
        for (uint8_t i = 0; i < CUBIES; ++i)
            destination[face][source[face][i]] = i;
    for (uint8_t face = 0; face < 3; ++face)
        for (unsigned code = 0; code < codes; ++code) {
            uint8_t position[MAX_SUBSET] = {0};
            unsigned rest = code;
            for (uint8_t i = subset_size; i-- > 0;) {
                position[i] = destination[face][rest % 7];
                rest /= 7;
            }
            subset_move[face][code] = subset_code(position);
        }
    memset(subset_distance, UINT8_MAX, sizeof subset_distance);
    queue[0] = subset_code(subset);
    subset_distance[queue[0]] = 0;
    *maximum = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        uint16_t o = (uint16_t) (here / SUBSET_CODES);
        uint16_t s = (uint16_t) (here % SUBSET_CODES);
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t next_o = o, next_s = s;
            for (uint8_t turn = 0; turn < 3; ++turn) {
                next_o = orientation[face][next_o];
                next_s = subset_move[face][next_s];
                uint32_t there = (uint32_t) next_o * SUBSET_CODES + next_s;
                if (subset_distance[there] == UINT8_MAX) {
                    subset_distance[there] =
                        (uint8_t) (subset_distance[here] + 1);
                    if (subset_distance[there] > *maximum)
                        *maximum = subset_distance[there];
                    queue[tail++] = there;
                }
            }
        }
    }
    return tail;
}

static unsigned bench_heuristic(const variant_t *variant, uint16_t p,
                                uint16_t o, uint16_t s)
{
    unsigned h = 0;
    if (variant->use_p && permutation_distance[p] > h)
        h = permutation_distance[p];
    if (variant->use_o && orientation_distance[o] > h)
        h = orientation_distance[o];
    if (variant->use_s && subset_distance[o * SUBSET_CODES + s] > h)
        h = subset_distance[o * SUBSET_CODES + s];
    return h;
}

/* solve() from solver_ida.c with counters, a pruning switch and the subset
 * coordinate carried alongside. expanded counts every frame entered,
 * including the root of each round; candidates counts every move tried,
 * including those the heuristic cuts.
 */
static int bench_solve(const variant_t *variant, uint16_t p, uint16_t o,
                       uint16_t s, solution_t *solution, counts_t *counts)
{
    bench_frame_t stack[MAX_DEPTH + 1];
    memset(solution, 0, sizeof *solution);
    memset(counts, 0, sizeof *counts);
    for (unsigned bound = bench_heuristic(variant, p, o, s);
         bound <= MAX_DEPTH; ++bound) {
        unsigned depth = 0;
        stack[0] = (bench_frame_t) {p, o, s, p, o, s, 0, 0, 3};
        ++counts->iterations;
        ++counts->expanded;
        if (p == 0 && o == 0)
            return 1;
        for (;;) {
            bench_frame_t *frame = &stack[depth];
            if (frame->face == 3) {
                if (depth == 0)
                    break;
                --depth;
                continue;
            }
            if ((variant->prune && frame->face == frame->previous_face) ||
                frame->turn == 3) {
                ++frame->face;
                frame->turn = 0;
                frame->next_p = frame->p;
                frame->next_o = frame->o;
                frame->next_s = frame->s;
                continue;
            }
            uint8_t face = frame->face;
            frame->next_p = permutation[face][frame->next_p];
            frame->next_o = orientation[face][frame->next_o];
            if (variant->use_s)
                frame->next_s = subset_move[face][frame->next_s];
            uint8_t move = (uint8_t) (face * 3U + frame->turn++);
            ++counts->candidates;
            uint16_t next_p = frame->next_p, next_o = frame->next_o;
            uint16_t next_s = frame->next_s;
            if (depth + 1 + bench_heuristic(variant, next_p, next_o, next_s) >
                bound)
                continue;
            solution->moves[depth] = move;
            if (next_p == 0 && next_o == 0) {
                solution->length = depth + 1;
                return 1;
            }
            ++depth;
            stack[depth] = (bench_frame_t) {next_p, next_o, next_s, next_p,
                                            next_o, next_s, 0,      0,
                                            face};
            ++counts->expanded;
        }
    }
    return 0;
}

static int admissible(const variant_t *variant, const uint8_t *distance)
{
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint16_t s = 0;
        if (variant->use_s) {
            state_t state;
            unrank_state(rank, &state);
            s = state_subset_code(&state);
        }
        if (bench_heuristic(variant, (uint16_t) (rank / ORIENTATIONS),
                            (uint16_t) (rank % ORIENTATIONS),
                            s) > distance[rank])
            return 0;
    }
    return 1;
}

/* Target table sizes, with subset positions ranked densely. */
static void table_bytes(const variant_t *variant, unsigned *unpacked,
                        unsigned *packed)
{
    unsigned base = sizeof permutation + sizeof orientation;
    unsigned entries[3] = {variant->use_p ? PERMUTATIONS : 0,
                           variant->use_o ? ORIENTATIONS : 0, 0};
    if (variant->use_s) {
        unsigned k = arrangements(subset_size);
        entries[2] = ORIENTATIONS * k;
        base += 3 * k * (k > 256 ? 2 : 1);
    }
    *unpacked = *packed = base;
    for (unsigned i = 0; i < 3; ++i) {
        *unpacked += entries[i];
        *packed += (entries[i] + 1) / 2;
    }
}

static void state_string(uint32_t rank, char text[15])
{
    state_t state;
    unrank_state(rank, &state);
    for (unsigned i = 0; i < CUBIES; ++i) {
        text[i] = (char) ('1' + state.p[i]);
        text[i + CUBIES] = (char) ('1' + state.o[i]);
    }
    text[14] = '\0';
}

static int parse_subset(const char *text)
{
    size_t length = strlen(text);
    if (length < 1 || length > MAX_SUBSET)
        return 0;
    subset_size = (uint8_t) length;
    for (uint8_t i = 0; i < subset_size; ++i) {
        if (text[i] < '1' || text[i] > '7')
            return 0;
        subset[i] = (uint8_t) (text[i] - '1');
        for (uint8_t j = 0; j < i; ++j)
            if (subset[j] == subset[i])
                return 0;
    }
    return 1;
}

static int usage(const char *program)
{
    fprintf(stderr,
            "usage: %s [--depth D] [--subset CUBIES] [--csv FILE] VARIANT...\n"
            "variants: max max-noprune perm orient os\n",
            program);
    return 2;
}

/* Runs one variant over the target states, prints its summary line and
 * returns 0 if every check passed.
 */
static int run_variant(const variant_t *variant, const uint8_t *distance,
                       const uint32_t *targets, uint32_t count, FILE *csv)
{
    char name[32], text[15];
    unsigned unpacked, packed;
    uint64_t max_expanded = 0, max_candidates = 0, total_candidates = 0;
    uint32_t worst_rank = targets[0];
    if (variant->use_s) {
        char digits[MAX_SUBSET + 1] = {0};
        for (uint8_t i = 0; i < subset_size; ++i)
            digits[i] = (char) ('1' + subset[i]);
        snprintf(name, sizeof name, "%s{%s}", variant->name, digits);
    } else
        snprintf(name, sizeof name, "%s", variant->name);
    table_bytes(variant, &unpacked, &packed);
    if (!admissible(variant, distance)) {
        printf("%-14s %12u %13u  H1 FAILED, search skipped\n", name,
               unpacked, packed);
        return 1;
    }
    clock_t started = clock();
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t rank = targets[i];
        uint16_t p = (uint16_t) (rank / ORIENTATIONS);
        uint16_t o = (uint16_t) (rank % ORIENTATIONS);
        state_t state;
        solution_t solution;
        counts_t counts;
        unrank_state(rank, &state);
        uint16_t s = variant->use_s ? state_subset_code(&state) : 0;
        if (!bench_solve(variant, p, o, s, &solution, &counts) ||
            solution.length != distance[rank] ||
            !verify_solution(state, &solution)) {
            state_string(rank, text);
            fprintf(stderr, "%s: wrong path for %s\n", name, text);
            return 1;
        }
        total_candidates += counts.candidates;
        if (counts.expanded > max_expanded)
            max_expanded = counts.expanded;
        if (counts.candidates > max_candidates) {
            max_candidates = counts.candidates;
            worst_rank = rank;
        }
        if (csv) {
            state_string(rank, text);
            fprintf(csv, "%s,%s,%u,%llu,%llu,%u\n", name, text,
                    distance[rank], (unsigned long long) counts.expanded,
                    (unsigned long long) counts.candidates, counts.iterations);
        }
        if ((i + 1) % 100000 == 0)
            fprintf(stderr, "%s: %u/%u states\n", name, i + 1, count);
    }
    double seconds = (double) (clock() - started) / CLOCKS_PER_SEC;
    if (variant == &variants[0])
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t rank = targets[i];
            uint16_t p = (uint16_t) (rank / ORIENTATIONS);
            uint16_t o = (uint16_t) (rank % ORIENTATIONS);
            solution_t solution, reference;
            counts_t counts;
            if (!bench_solve(variant, p, o, 0, &solution, &counts) ||
                !solve(p, o, &reference) ||
                reference.length != solution.length ||
                memcmp(reference.moves, solution.moves, solution.length)) {
                state_string(rank, text);
                fprintf(stderr, "%s: differs from solve() for %s\n", name,
                        text);
                return 1;
            }
        }
    state_string(worst_rank, text);
    printf("%-14s %12u %13u %8u %10llu %11llu %13.1f  %s %8.2f\n", name,
           unpacked, packed, count, (unsigned long long) max_expanded,
           (unsigned long long) max_candidates,
           (double) total_candidates / count, text, seconds);
    return 0;
}

int main(int argc, char **argv)
{
    const char *program = argc > 0 && argv[0] ? argv[0] : "ida_bench";
    const char *subset_text = "123", *csv_path = NULL;
    const variant_t *chosen[VARIANTS];
    unsigned depth = 11, chosen_count = 0, need_subset = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--depth") && i + 1 < argc) {
            const char *text = argv[++i];
            char *end;
            unsigned long value = strtoul(text, &end, 10);
            if (end == text || *end || value > MAX_DEPTH)
                return usage(program);
            depth = (unsigned) value;
        } else if (!strcmp(argv[i], "--subset") && i + 1 < argc)
            subset_text = argv[++i];
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc)
            csv_path = argv[++i];
        else {
            unsigned v = 0;
            while (v < VARIANTS && strcmp(argv[i], variants[v].name))
                ++v;
            if (v == VARIANTS || chosen_count == VARIANTS)
                return usage(program);
            chosen[chosen_count++] = &variants[v];
            need_subset |= variants[v].use_s;
        }
    }
    if (!chosen_count || !parse_subset(subset_text))
        return usage(program);

    uint8_t diameter;
    if (!init_tables()) {
        fputs("incomplete heuristic tables\n", stderr);
        return 1;
    }
    uint8_t *distance = build_table(&diameter);
    if (!distance || diameter != 11) {
        fputs("BFS oracle failed\n", stderr);
        free(distance);
        return 1;
    }
    if (need_subset) {
        unsigned maximum;
        uint32_t reached = build_subset(&maximum);
        uint32_t expected = ORIENTATIONS * arrangements(subset_size);
        printf("subset {%s}: %u of %u (orientation, position) pairs reached, "
               "max distance %u\n",
               subset_text, reached, expected, maximum);
        if (reached != expected) {
            free(distance);
            return 1;
        }
    }

    uint32_t count = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank)
        count += distance[rank] == depth;
    uint32_t *targets = malloc((size_t) count * sizeof *targets);
    if (!targets) {
        free(distance);
        return 1;
    }
    count = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank)
        if (distance[rank] == depth)
            targets[count++] = rank;

    FILE *csv = NULL;
    if (csv_path) {
        csv = fopen(csv_path, "w");
        if (!csv) {
            perror(csv_path);
            free(targets);
            free(distance);
            return 1;
        }
        fputs("variant,state,distance,expanded,candidates,iterations\n", csv);
    }
    printf("%-14s %12s %13s %8s %10s %11s %13s  %-14s %8s\n", "variant",
           "target_bytes", "target_bytes4", "states", "max_exp", "max_cand",
           "mean_cand", "worst_state", "seconds");
    int failed = 0;
    for (unsigned i = 0; i < chosen_count; ++i)
        failed |= run_variant(chosen[i], distance, targets, count, csv);
    if (csv) {
        int write_error = ferror(csv);
        if (fclose(csv) != 0 || write_error)
            failed = 1;
    }
    free(targets);
    free(distance);
    return failed || output_failed();
}
