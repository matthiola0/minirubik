/* Search from commit 3508278, with operation counters only. */
static frame_t new_frame(uint16_t p, uint16_t o, uint8_t previous_face)
{
    frame_t frame = {p, o, p, o, 0, 0, previous_face};
    return frame;
}

/* Fixed stack, no recursion, allocator, rank/unrank or division in search. */
static int solve_stage2(uint16_t p, uint16_t o, solution_t *solution)
{
    frame_t stack[MAX_DEPTH + 1];
    memset(solution, 0, sizeof *solution);
    for (unsigned bound = heuristic(p, o); bound <= MAX_DEPTH; ++bound) {
        unsigned depth = 0;
        stack[0] = new_frame(p, o, 3);
        IDA_COUNT(iterations);
        IDA_COUNT(expanded);
        if (p == 0 && o == 0)
            return 1;
        for (;;) {
            frame_t *frame = &stack[depth];
            if (frame->face == 3) {
                if (depth == 0)
                    break;
                --depth;
                continue;
            }
            if (frame->face == frame->previous_face || frame->turn == 3) {
                ++frame->face;
                frame->turn = 0;
                frame->next_p = frame->p;
                frame->next_o = frame->o;
                continue;
            }
            IDA_COUNT(candidates);
            IDA_COUNT(p_reads);
            IDA_COUNT(o_reads);
            uint8_t face = frame->face;
            frame->next_p = permutation[face][frame->next_p];
            frame->next_o = orientation[face][frame->next_o];
            uint8_t move = (uint8_t) (face * 3U + frame->turn++);
            uint16_t next_p = frame->next_p, next_o = frame->next_o;
            if (depth + 1 + heuristic(next_p, next_o) > bound)
                continue;
            solution->moves[depth] = move;
            if (next_p == 0 && next_o == 0) {
                solution->length = depth + 1;
                return 1;
            }
            ++depth;
            stack[depth] = new_frame(next_p, next_o, face);
            IDA_COUNT(expanded);
        }
    }
    return 0;
}

