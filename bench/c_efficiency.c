/* Compare solver_ida.c with search_baseline.h on every distance-11 state.
 * The baseline preserves the search from commit 3508278 with counters added.
 * Each query must return identical optimal paths, iteration counts, expanded
 * counts and candidate counts, and replay to solved through the cubie model.
 * Baseline output rows retain the historical label "stage2".
 *
 * From the repository root on Windows:
 *   ./bench/run_efficiency.ps1
 * This builds three heuristic-order variants at -O3, checks the CLI with an
 * independent Python cubie model, compares each variant with the baseline,
 * repeats the default comparison at -O2, and runs exhaustive H1-H3 checks.
 * H4 is not applicable because distances remain byte arrays. Output and
 * per-state CSVs go to bench/results/. The self-test takes a few minutes.
 *
 * On other hosts:
 *   make solver_ida bench/c_efficiency
 *   ./bench/c_efficiency p-first.csv
 *   ./solver_ida --self-test
 *   python3 tests/check_ida_cli.py
 * Usage: c_efficiency [output.csv]
 * Compile with -DIDA_HEURISTIC_ORDER=0 for eager max, =1 for permutation first
 * (the default), or =2 for orientation first. Only bound-check order and
 * short-circuiting change; the search tree and transition tables are the same.
 *
 * Counter definitions:
 *   iterations: IDA* rounds started.
 *   expanded: root or non-goal child entered.
 *   candidates: candidate moves tried, including rejected moves.
 *   p_reads, o_reads: candidate distance reads; exclude root bound/setup.
 *   face_starts: allowed face groups started; exclude backtracking resumes.
 *   saves, restores: parent frames saved before descent or restored on return.
 * The baseline records zero for face_starts, saves and restores because those
 * events are not instrumented there; zero does not mean no frame writes occur.
 * Each candidate uses two transition reads in all variants. An ordinary
 * solver build removes all counters.
 *
 * These are C operation counts, not processor loads/stores or retired
 * instructions. Locals may spill; compilers may combine or eliminate accesses.
 * They do not establish Ripes instruction budgets or assembly speed. Target
 * .data + .bss + .rodata, .text and --iret are measured separately in rv32/.
 * The four search tables occupy 40,383 bytes; the fixed frame array uses 144
 * bytes and solution_t uses 16 bytes on the measured host. These exclude host
 * table construction, the exhaustive oracle, CLI/runtime data and stack spills.
 * Table generation and BFS validation use host allocation and division and
 * are not linked into the target search.
 */
#include <stdint.h>
#include <inttypes.h>

typedef struct {
    uint64_t iterations, expanded, candidates, p_reads, o_reads;
    uint64_t face_starts, saves, restores;
} counts_t;
static counts_t counts;
#define IDA_COUNT(event) (++counts.event)
#define main solver_ida_main
#include "../solver_ida.c"
#undef main
#include "search_baseline.h"

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

static void print_counts(FILE *out, const char *variant, const char *state,
                         counts_t c)
{
    fprintf(out, "%s,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%" PRIu64 "\n", variant, state, c.iterations, c.expanded,
            c.candidates, c.p_reads, c.o_reads, c.face_starts, c.saves,
            c.restores);
}

static void add_counts(counts_t *total, counts_t c)
{
    total->iterations += c.iterations;
    total->expanded += c.expanded;
    total->candidates += c.candidates;
    total->p_reads += c.p_reads;
    total->o_reads += c.o_reads;
    total->face_starts += c.face_starts;
    total->saves += c.saves;
    total->restores += c.restores;
}

int main(int argc, char **argv)
{
    if (argc > 2)
        return 2;
    uint8_t diameter;
    if (!init_tables())
        return 1;
    uint8_t *distance = build_table(&diameter);
    if (!distance || diameter != 11) {
        free(distance);
        return 1;
    }
    FILE *csv = argc == 2 ? fopen(argv[1], "w") : NULL;
    if (argc == 2 && !csv) {
        perror(argv[1]);
        free(distance);
        return 1;
    }
    const char *header = "variant,state,iterations,expanded,candidates,"
                         "p_reads,o_reads,face_starts,saves,restores\n";
    const char *name = IDA_HEURISTIC_ORDER == 0 ? "eager" :
                       IDA_HEURISTIC_ORDER == 1 ? "p-first" : "o-first";
    fputs(header, stdout);
    if (csv)
        fputs(header, csv);
    counts_t totals[2] = {{0}, {0}}, worst = {0};
    unsigned checked = 0;
    uint32_t worst_rank = 0;
    int failed = 0;
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        if (distance[rank] != 11)
            continue;
        uint16_t p = (uint16_t) (rank / ORIENTATIONS);
        uint16_t o = (uint16_t) (rank % ORIENTATIONS);
        solution_t before, after;
        counts = (counts_t) {0};
        int ok_before = solve_stage2(p, o, &before);
        counts_t base = counts;
        counts = (counts_t) {0};
        int ok_after = solve(p, o, &after);
        counts_t current = counts;
        state_t state;
        char text[15];
        unrank_state(rank, &state);
        state_string(rank, text);
        if (!ok_before || !ok_after || before.length != 11 ||
            after.length != 11 || memcmp(before.moves, after.moves, 11) ||
            !verify_solution(state, &after) ||
            base.candidates != current.candidates ||
            base.expanded != current.expanded ||
            base.iterations != current.iterations) {
            fprintf(stderr, "comparison failed: %s\n", text);
            failed = 1;
            break;
        }
        add_counts(&totals[0], base);
        add_counts(&totals[1], current);
        if (current.candidates > worst.candidates) {
            worst = current;
            worst_rank = rank;
        }
        if (csv) {
            print_counts(csv, "stage2", text, base);
            print_counts(csv, name, text, current);
        }
        if (!strcmp(text, "21345671111111")) {
            print_counts(stdout, "stage2", text, base);
            print_counts(stdout, name, text, current);
        }
        ++checked;
    }
    char text[15];
    state_string(worst_rank, text);
    print_counts(stdout, name, text, worst);
    print_counts(stdout, "stage2", "TOTAL", totals[0]);
    print_counts(stdout, name, "TOTAL", totals[1]);
    printf("%u distance-11 states: identical optimal paths and search counts\n",
           checked);
    printf("tables=%zu bytes; frame=%zu; stack=%zu; solution=%zu\n",
           sizeof permutation + sizeof orientation +
               sizeof permutation_distance + sizeof orientation_distance,
           sizeof(frame_t), sizeof(frame_t) * (MAX_DEPTH + 1),
           sizeof(solution_t));
    if (csv) {
        int write_error = ferror(csv);
        if (fclose(csv) || write_error)
            failed = 1;
    }
    free(distance);
    return failed || checked != 2644 || output_failed();
}
