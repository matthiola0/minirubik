/* Exhaustive check of the actual freestanding C search and input ranking.
 * The complete BFS distance table is host-only and never linked on target.
 */
#include <time.h>
#define main oracle_main
#include "../solver_ida.c"
#undef main
#define HOST_VERIFY
#define CUBIES ref_CUBIES
#define MAX_DEPTH ref_MAX_DEPTH
#define state_t ref_state_t
#define frame_t ref_frame_t
#define solution_t ref_solution_t
#define source ref_source
#define twist ref_twist
#define input ref_input
#define move_names ref_move_names
#define permutation ref_permutation
#define orientation ref_orientation
#define permutation_distance ref_permutation_distance
#define orientation_distance ref_orientation_distance
#define parse_state ref_parse_state
#define solve ref_solve
#define heuristic ref_heuristic
#include "solver_ref.c"
#undef CUBIES
#undef MAX_DEPTH
#undef state_t
#undef frame_t
#undef solution_t
#undef source
#undef twist
#undef input
#undef move_names
#undef permutation
#undef orientation
#undef permutation_distance
#undef orientation_distance
#undef parse_state
#undef solve
#undef heuristic

int main(void)
{
    if (!init_tables()) return 1;
    if (memcmp(permutation, ref_permutation, sizeof permutation) ||
        memcmp(orientation, ref_orientation, sizeof orientation) ||
        memcmp(permutation_distance, ref_permutation_distance,
               sizeof permutation_distance) ||
        memcmp(orientation_distance, ref_orientation_distance,
               sizeof orientation_distance)) {
        fputs("generated tables differ from host reference\n", stderr);
        return 1;
    }
    uint8_t diameter;
    uint8_t *distance = build_table(&diameter);
    if (!distance || diameter != 11) return 1;
    puts("H2: generated tables match every host entry; solved=0; maxima P=7 O=6");
    fflush(stdout);
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        state_t state;
        ref_state_t parsed;
        ref_solution_t solution;
        char text[15];
        uint16_t p, o;
        unrank_state(rank, &state);
        for (unsigned k = 0; k < 7; ++k) {
            text[k] = '1' + state.p[k];
            text[k + 7] = '1' + state.o[k];
        }
        text[14] = 0;
        if (!ref_parse_state(text, &parsed)) return 1;
        rank_coordinates(&parsed, &p, &o);
        if ((uint32_t)p * ORIENTATIONS + o != rank ||
            ref_heuristic(p, o) > distance[rank] ||
            !ref_solve(p, o, &solution) || solution.length != distance[rank] ||
            !replay_solves(parsed, &solution)) {
            fprintf(stderr, "FAIL rank=%u state=%s\n", rank, text);
            free(distance);
            return 1;
        }
        if (rank && rank % 500000 == 0) {
            printf("%u/%u states\n", rank, STATES);
            fflush(stdout);
        }
    }
    free(distance);
    puts("PASS: parse/rank, H1 and H3, all 3674160 states; diameter 11; H4 N/A (byte distances)");
    return 0;
}
