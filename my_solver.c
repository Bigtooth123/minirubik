#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    STATES = PERMUTATIONS * ORIENTATIONS,
    FACES = 3,
    MOVES = 9,
    MAX_SOLUTION_LENGTH = 11,
    IDA_STACK_DEPTH = MAX_SOLUTION_LENGTH + 1
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

typedef struct {
    uint16_t p;
    uint16_t o;
} search_state_t;

typedef struct {
    uint16_t p;
    uint16_t o;
    uint8_t next_move;
    uint8_t last_face;
} ida_frame_t;

typedef char ida_frame_must_be_six_bytes[
    sizeof(ida_frame_t) == 6 ? 1 : -1];

static const char *const move_names[MOVES] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};

static const uint8_t inverse_move[MOVES] = {
    2, 1, 0, 5, 4, 3, 8, 7, 6
};

/* Avoid division and remainder when decoding a move on RV32I. */
static const uint8_t move_face[MOVES] = {
    0, 0, 0, 1, 1, 1, 2, 2, 2
};

static const uint8_t move_turns[MOVES] = {
    1, 2, 3, 1, 2, 3, 1, 2, 3
};

/* Each destination takes a cubie from source[face][destination]. */
static const uint8_t source[FACES][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};

static const uint8_t twist[FACES][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

/*
 * The host generator needs writable storage while constructing the tables.
 * Normal solver builds include the same data as compile-time constants.
 */
#ifdef STATIC_TABLE_GENERATOR
static uint16_t permutation[FACES][PERMUTATIONS];
static uint16_t orientation[FACES][ORIENTATIONS];
static uint8_t perm_dist[PERMUTATIONS];
static uint8_t ori_dist[ORIENTATIONS];
#else
#include "static_tables.h"
#endif

static const uint16_t *const permutation_face[FACES] = {
    permutation[0], permutation[1], permutation[2]
};

static const uint16_t *const orientation_face[FACES] = {
    orientation[0], orientation[1], orientation[2]
};

static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        uint8_t o = (uint8_t) (state.o[from] + twist[face][i]);

        if (o >= 3U)
            o = (uint8_t) (o - 3U);

        result.p[i] = state.p[from];
        result.o[i] = o;
    }

    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t face = move_face[move];
    uint8_t turns = move_turns[move];

    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, face);

    return state;
}

static uint8_t count_smaller_after(const state_t *state, uint8_t position)
{
    uint8_t smaller = 0;

    for (uint8_t i = (uint8_t) (position + 1U); i < CUBIES; ++i) {
        if (state->p[i] < state->p[position])
            ++smaller;
    }

    return smaller;
}

static uint16_t rank_permutation(const state_t *state)
{
    uint16_t p = count_smaller_after(state, 0);

    p = (uint16_t) ((p << 2U) + (p << 1U) +
                    count_smaller_after(state, 1));
    p = (uint16_t) ((p << 2U) + p +
                    count_smaller_after(state, 2));
    p = (uint16_t) ((p << 2U) + count_smaller_after(state, 3));
    p = (uint16_t) ((p << 1U) + p +
                    count_smaller_after(state, 4));
    p = (uint16_t) ((p << 1U) + count_smaller_after(state, 5));

    return p;
}

static uint16_t rank_orientation(const state_t *state)
{
    uint16_t o = 0;

    for (uint8_t i = 0; i < 6; ++i)
        o = (uint16_t) ((o << 1U) + o + state->o[i]);

    return o;
}

static uint32_t rank_state(const state_t *state)
{
    uint32_t p = rank_permutation(state);
    uint32_t o = rank_orientation(state);

    return p * ORIENTATIONS + o;
}

/* Dense-rank decoding is used only by host-side generation and tests. */
static void unrank_state(uint32_t rank, state_t *state)
{
    uint8_t available[CUBIES] = {0, 1, 2, 3, 4, 5, 6};
    uint32_t p = rank / ORIENTATIONS;
    uint32_t o = rank % ORIENTATIONS;
    uint32_t f = 720;
    uint8_t sum = 0;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t q = (uint8_t) (p / f);
        p %= f;

        state->p[i] = available[q];

        for (uint8_t j = q; j + 1U < (unsigned) CUBIES - i; ++j)
            available[j] = available[j + 1U];

        if (i < 5)
            f /= 6U - i;
    }

    for (uint8_t i = 6; i-- > 0;) {
        state->o[i] = (uint8_t) (o % 3U);
        sum = (uint8_t) (sum + state->o[i]);
        o /= 3U;
    }

    while (sum >= 3U)
        sum = (uint8_t) (sum - 3U);

    state->o[6] = sum == 0 ? 0 : (uint8_t) (3U - sum);
}

static int valid(const state_t *state)
{
    uint8_t sum = 0;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3)
            return 0;

        for (uint8_t j = 0; j < i; ++j) {
            if (state->p[j] == state->p[i])
                return 0;
        }

        sum = (uint8_t) (sum + state->o[i]);
    }

    return sum == 0 || sum == 3 || sum == 6 ||
           sum == 9 || sum == 12;
}

static int parse_state(const char *input, state_t *state)
{
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (input[i] < '1' || input[i] > '7')
            return 0;

        state->p[i] = (uint8_t) (input[i] - '1');
    }

    for (uint8_t i = 0; i < CUBIES; ++i) {
        char digit = input[CUBIES + i];

        if (digit < '1' || digit > '3')
            return 0;

        state->o[i] = (uint8_t) (digit - '1');
    }

    return input[14] == '\0' && valid(state);
}

static search_state_t split_rank(uint32_t rank)
{
    search_state_t state;

    state.p = (uint16_t) (rank / ORIENTATIONS);
    state.o = (uint16_t) (rank % ORIENTATIONS);

    return state;
}

static search_state_t encode_search_state(const state_t *state)
{
    search_state_t result;

    /* Keep the components separate instead of dividing a dense rank by 729. */
    result.p = rank_permutation(state);
    result.o = rank_orientation(state);
    return result;
}

#ifdef STATIC_TABLE_GENERATOR
/* Generate the factored quarter-turn transition tables on the host. */
static void build_transition_tables(void)
{
    state_t state;

    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        unrank_state((uint32_t) p * ORIENTATIONS, &state);

        for (uint8_t face = 0; face < FACES; ++face) {
            state_t next = quarter_turn(state, face);
            permutation[face][p] =
                (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        unrank_state(o, &state);

        for (uint8_t face = 0; face < FACES; ++face) {
            state_t next = quarter_turn(state, face);
            orientation[face][o] =
                (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
}
#endif

#ifdef STATIC_TABLE_GENERATOR
static uint16_t apply_perm_move(uint16_t p, uint8_t move)
{
    const uint16_t *table = permutation_face[move_face[move]];
    uint8_t turns = move_turns[move];

    do {
        p = table[p];
    } while (--turns != 0);

    return p;
}

static uint16_t apply_ori_move(uint16_t o, uint8_t move)
{
    const uint16_t *table = orientation_face[move_face[move]];
    uint8_t turns = move_turns[move];

    do {
        o = table[o];
    } while (--turns != 0);

    return o;
}
#endif

static search_state_t apply_search_move(search_state_t state, uint8_t move)
{
    uint8_t face = move_face[move];
    uint8_t turns = move_turns[move];
    const uint16_t *p_table = permutation_face[face];
    const uint16_t *o_table = orientation_face[face];

    do {
        state.p = p_table[state.p];
        state.o = o_table[state.o];
    } while (--turns != 0);

    return state;
}

#ifdef STATIC_TABLE_GENERATOR
/*
 * Build exact distances in the permutation abstraction.
 *
 * The queue only exists on the host while generating this table.
 */
static void build_perm_heuristic(void)
{
    uint16_t queue[PERMUTATIONS];
    uint16_t head = 0, tail = 0;

    memset(perm_dist, UINT8_MAX, sizeof perm_dist);

    perm_dist[0] = 0;
    queue[tail++] = 0;

    while (head < tail) {
        uint16_t p = queue[head++];
        uint8_t next_depth = (uint8_t) (perm_dist[p] + 1U);

        for (uint8_t move = 0; move < MOVES; ++move) {
            uint16_t next = apply_perm_move(p, move);

            if (perm_dist[next] == UINT8_MAX) {
                perm_dist[next] = next_depth;
                queue[tail++] = next;
            }
        }
    }
}

/*
 * Build exact distances in the orientation abstraction.
 */
static void build_ori_heuristic(void)
{
    uint16_t queue[ORIENTATIONS];
    uint16_t head = 0, tail = 0;

    memset(ori_dist, UINT8_MAX, sizeof ori_dist);

    ori_dist[0] = 0;
    queue[tail++] = 0;

    while (head < tail) {
        uint16_t o = queue[head++];
        uint8_t next_depth = (uint8_t) (ori_dist[o] + 1U);

        for (uint8_t move = 0; move < MOVES; ++move) {
            uint16_t next = apply_ori_move(o, move);

            if (ori_dist[next] == UINT8_MAX) {
                ori_dist[next] = next_depth;
                queue[tail++] = next;
            }
        }
    }
}

static void build_heuristic_tables(void)
{
    build_perm_heuristic();
    build_ori_heuristic();
}
#endif

static uint8_t heuristic(search_state_t state)
{
    uint8_t hp = perm_dist[state.p];
    uint8_t ho = ori_dist[state.o];

    return hp > ho ? hp : ho;
}

/*
 * Run an IDA* search without recursion or heap allocation.
 *
 * Each threshold iteration is a depth-first traversal represented by a fixed
 * stack.  Consecutive turns of the same face are skipped because they can
 * always be combined into one HTM move or cancel completely.
 */
static int ida_star(search_state_t start, uint8_t *solution,
                    uint8_t *solution_length)
{
    ida_frame_t stack[IDA_STACK_DEPTH];
    uint8_t bound = heuristic(start);

    if (start.p == 0 && start.o == 0) {
        *solution_length = 0;
        return 1;
    }

    while (bound <= MAX_SOLUTION_LENGTH) {
        uint8_t depth = 0;
        uint8_t next_bound = UINT8_MAX;
        ida_frame_t *frame = &stack[0];

        frame->p = start.p;
        frame->o = start.o;
        frame->next_move = 0;
        frame->last_face = FACES;

        for (;;) {
            if (frame->next_move == MOVES) {
                if (depth == 0)
                    break;

                --depth;
                --frame;
                continue;
            }

            uint8_t move = frame->next_move++;
            uint8_t face = move_face[move];

            if (face == frame->last_face)
                continue;

            search_state_t next = {frame->p, frame->o};
            next = apply_search_move(next, move);

            uint8_t next_depth = (uint8_t) (depth + 1U);

            solution[depth] = move;

            if (next.p == 0 && next.o == 0) {
                *solution_length = next_depth;
                return 1;
            }

            uint8_t estimate = (uint8_t) (next_depth + heuristic(next));

            if (estimate > bound) {
                if (estimate < next_bound)
                    next_bound = estimate;

                continue;
            }

            ++frame;
            frame->p = next.p;
            frame->o = next.o;
            frame->next_move = 0;
            frame->last_face = face;
            depth = next_depth;
        }

        if (next_bound == UINT8_MAX)
            return 0;

        bound = next_bound;
    }

    return 0;
}

/*
 * Baseline exhaustive BFS.
 *
 * exact_distance is optional. If non-NULL, the exact HTM distance
 * of every full state is recorded for H1.
 */
static uint8_t *build_table(uint8_t *diameter, uint8_t *exact_distance)
{
    uint8_t *toward_solved = malloc(STATES);
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);
    uint32_t head = 0, tail = 1, level_end = 1;

    if (!toward_solved || !queue) {
        free(toward_solved);
        free(queue);
        return NULL;
    }

    memset(toward_solved, UINT8_MAX, STATES);

    if (exact_distance)
        memset(exact_distance, UINT8_MAX, STATES);

    queue[0] = 0;
    toward_solved[0] = 0;

    if (exact_distance)
        exact_distance[0] = 0;

    *diameter = 0;

    while (head < tail) {
        if (head == level_end) {
            level_end = tail;
            ++*diameter;
        }

        uint32_t here = queue[head++];
        search_state_t current = split_rank(here);

        for (uint8_t face = 0; face < FACES; ++face) {
            uint16_t next_p = current.p;
            uint16_t next_o = current.o;

            for (uint8_t turn = 0; turn < 3; ++turn) {
                next_p = permutation[face][next_p];
                next_o = orientation[face][next_o];

                uint32_t there =
                    (uint32_t) next_p * ORIENTATIONS + next_o;

                if (toward_solved[there] == UINT8_MAX) {
                    uint8_t move = (uint8_t) (face * 3U + turn);

                    toward_solved[there] = inverse_move[move];
                    queue[tail++] = there;

                    if (exact_distance)
                        exact_distance[there] =
                            (uint8_t) (exact_distance[here] + 1U);
                }
            }
        }
    }

    free(queue);

    if (tail != STATES) {
        free(toward_solved);
        return NULL;
    }

    return toward_solved;
}

static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

/*
 * Original move/inverse sanity check.
 */
static int test_move_inverse(void)
{
    const state_t solved = {
        {0, 1, 2, 3, 4, 5, 6},
        {0, 0, 0, 0, 0, 0, 0}
    };

    for (uint8_t move = 0; move < MOVES; ++move) {
        state_t state = solved;

        state = apply_move(state, move);
        state = apply_move(state, inverse_move[move]);

        if (memcmp(&solved, &state, sizeof solved) != 0) {
            fprintf(stderr, "inverse test failed for %s\n",
                    move_names[move]);
            return 0;
        }
    }

    return 1;
}

static int test_rank_roundtrip(void)
{
    state_t state;

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        unrank_state(rank, &state);

        if (!valid(&state)) {
            fprintf(stderr, "invalid state from rank %u\n", rank);
            return 0;
        }

        if (rank_state(&state) != rank) {
            fprintf(stderr, "rank/unrank mismatch at %u\n", rank);
            return 0;
        }
    }

    return 1;
}

static int test_transition_bounds(void)
{
    for (uint8_t face = 0; face < FACES; ++face) {
        for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
            if (permutation[face][p] >= PERMUTATIONS) {
                fprintf(stderr,
                        "invalid permutation transition: face=%u p=%u\n",
                        face, p);
                return 0;
            }
        }

        for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
            if (orientation[face][o] >= ORIENTATIONS) {
                fprintf(stderr,
                        "invalid orientation transition: face=%u o=%u\n",
                        face, o);
                return 0;
            }
        }
    }

    return 1;
}

static int test_transition_equivalence(int exhaustive)
{
    state_t state;
    uint32_t step = exhaustive ? 1U : 997U;

    for (uint32_t rank = 0; rank < STATES; rank += step) {
        unrank_state(rank, &state);

        search_state_t compact = encode_search_state(&state);

        for (uint8_t move = 0; move < MOVES; ++move) {
            state_t expected_state = apply_move(state, move);
            search_state_t expected =
                split_rank(rank_state(&expected_state));

            search_state_t actual =
                apply_search_move(compact, move);

            if (actual.p != expected.p || actual.o != expected.o) {
                fprintf(stderr,
                        "transition mismatch: rank=%u move=%s\n",
                        rank, move_names[move]);

                fprintf(stderr,
                        "expected (%u,%u), got (%u,%u)\n",
                        expected.p, expected.o,
                        actual.p, actual.o);

                return 0;
            }
        }
    }

    return 1;
}

/*
 * H2:
 *
 * Verify that both heuristic tables are completely populated,
 * their solved entries are zero, and report their maximum values.
 */
static int test_heuristic_tables(void)
{
    uint8_t max_perm = 0;
    uint8_t max_ori = 0;

    if (perm_dist[0] != 0) {
        fprintf(stderr,
                "H2 failed: perm_dist[0] = %u, expected 0\n",
                perm_dist[0]);
        return 0;
    }

    if (ori_dist[0] != 0) {
        fprintf(stderr,
                "H2 failed: ori_dist[0] = %u, expected 0\n",
                ori_dist[0]);
        return 0;
    }

    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        if (perm_dist[p] == UINT8_MAX) {
            fprintf(stderr,
                    "H2 failed: perm_dist[%u] is unvisited\n", p);
            return 0;
        }

        if (perm_dist[p] > max_perm)
            max_perm = perm_dist[p];
    }

    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        if (ori_dist[o] == UINT8_MAX) {
            fprintf(stderr,
                    "H2 failed: ori_dist[%u] is unvisited\n", o);
            return 0;
        }

        if (ori_dist[o] > max_ori)
            max_ori = ori_dist[o];
    }

    printf("H2: permutation heuristic max = %u\n", max_perm);
    printf("H2: orientation heuristic max = %u\n", max_ori);

    return 1;
}

/*
 * H1:
 *
 * Compare the heuristic against exact full-state BFS distances
 * over all 3,674,160 states.
 */
static int test_heuristic_admissibility(const uint8_t *exact_distance)
{
    uint32_t equality_count = 0;
    uint8_t max_h = 0;

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        search_state_t state = split_rank(rank);
        uint8_t h = heuristic(state);
        uint8_t d = exact_distance[rank];

        if (h > d) {
            fprintf(stderr,
                    "H1 failed at rank %u: h=%u exact=%u "
                    "(p=%u o=%u)\n",
                    rank, h, d, state.p, state.o);
            return 0;
        }

        if (h == d)
            ++equality_count;

        if (h > max_h)
            max_h = h;
    }

    printf("H1: checked %u states\n", (unsigned) STATES);
    printf("H1: maximum heuristic = %u\n", max_h);
    printf("H1: exact on %u states\n", equality_count);

    return 1;
}

/*
 * Exercise IDA* at several depths, including the HTM diameter.  Expected
 * depths come from independently generated exact BFS distances.  Replaying
 * every returned path against the physical state model checks that the moves
 * really solve the input rather than merely satisfying the compact model.
 */
static int test_ida_star_known_distances(void)
{
    static const struct {
        const char *input;
        uint8_t expected_depth;
    } cases[] = {
        {"12345671111111", 0},
        {"62345713133111", 8},
        {"24513763133333", 9},
        {"25416373331111", 10},
        {"21345671111111", 11},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        state_t state;
        uint8_t solution[MAX_SOLUTION_LENGTH];
        uint8_t length;

        if (!parse_state(cases[i].input, &state)) {
            fprintf(stderr, "IDA* test has invalid input: %s\n",
                    cases[i].input);
            return 0;
        }

        if (!ida_star(encode_search_state(&state), solution, &length)) {
            fprintf(stderr, "IDA* failed to solve %s\n", cases[i].input);
            return 0;
        }

        if (length != cases[i].expected_depth) {
            fprintf(stderr,
                    "IDA* depth mismatch for %s: got %u, expected %u\n",
                    cases[i].input, length, cases[i].expected_depth);
            return 0;
        }

        for (uint8_t move_index = 0; move_index < length; ++move_index) {
            uint8_t move = solution[move_index];

            if (move >= MOVES) {
                fprintf(stderr,
                        "IDA* returned invalid move %u for %s\n",
                        move, cases[i].input);
                return 0;
            }

            if (move_index > 0 &&
                move_face[solution[move_index - 1U]] == move_face[move]) {
                fprintf(stderr,
                        "IDA* returned consecutive same-face moves for %s\n",
                        cases[i].input);
                return 0;
            }

            state = apply_move(state, move);
        }

        if (rank_state(&state) != 0) {
            fprintf(stderr, "IDA* path did not solve %s\n", cases[i].input);
            return 0;
        }
    }

    printf("IDA*: solved %u known-distance cases (depths 0 through 11)\n",
           (unsigned) (sizeof cases / sizeof cases[0]));
    return 1;
}

/*
 * H3:
 *
 * Solve every full state with IDA* and compare the returned solution length
 * against the exact distance produced by the baseline BFS.  Replaying each
 * path in the compact model also verifies that the returned moves end at the
 * solved state.
 */
static int test_ida_star_full(const uint8_t *exact_distance)
{
    uint8_t solution[MAX_SOLUTION_LENGTH];

    for (uint32_t rank = 0; rank < STATES; ++rank) {
        search_state_t start = split_rank(rank);
        search_state_t state = start;
        uint8_t length;

        if (!ida_star(start, solution, &length)) {
            fprintf(stderr,
                    "H3 failed: IDA* could not solve rank %u "
                    "(p=%u o=%u)\n",
                    rank, start.p, start.o);
            return 0;
        }

        if (length != exact_distance[rank]) {
            fprintf(stderr,
                    "H3 failed at rank %u: IDA*=%u exact=%u "
                    "(p=%u o=%u)\n",
                    rank, length, exact_distance[rank],
                    start.p, start.o);
            return 0;
        }

        for (uint8_t move_index = 0; move_index < length; ++move_index) {
            uint8_t move = solution[move_index];

            if (move >= MOVES) {
                fprintf(stderr,
                        "H3 failed at rank %u: invalid move %u\n",
                        rank, move);
                return 0;
            }

            if (move_index > 0 &&
                move_face[solution[move_index - 1U]] == move_face[move]) {
                fprintf(stderr,
                        "H3 failed at rank %u: consecutive "
                        "same-face moves\n",
                        rank);
                return 0;
            }

            state = apply_search_move(state, move);
        }

        if (state.p != 0 || state.o != 0) {
            fprintf(stderr,
                    "H3 failed at rank %u: returned path did not solve "
                    "the state\n",
                    rank);
            return 0;
        }
    }

    printf("H3: IDA* matched exact BFS distances for %u states\n",
           (unsigned) STATES);
    return 1;
}

static int self_test(int exhaustive_transitions, int exhaustive_ida)
{
    uint8_t diameter;
    unsigned test_count = exhaustive_ida ? 8U : 7U;

    printf("[1/%u] move inverse\n", test_count);
    if (!test_move_inverse())
        return 0;

    printf("[2/%u] rank/unrank\n", test_count);
    if (!test_rank_roundtrip())
        return 0;

    printf("[3/%u] transition bounds\n", test_count);
    if (!test_transition_bounds())
        return 0;

    if (exhaustive_transitions)
        printf("[4/%u] exhaustive transition equivalence\n", test_count);
    else
        printf("[4/%u] sampled transition equivalence\n", test_count);

    if (!test_transition_equivalence(exhaustive_transitions))
        return 0;

    printf("[5/%u] H2 heuristic table sanity\n", test_count);
    if (!test_heuristic_tables())
        return 0;

    printf("[6/%u] H1 heuristic admissibility\n", test_count);

    uint8_t *exact_distance = malloc(STATES);

    if (!exact_distance) {
        fputs("could not allocate exact distance table\n", stderr);
        return 0;
    }

    uint8_t *table = build_table(&diameter, exact_distance);

    if (!table) {
        free(exact_distance);
        fputs("could not build complete state table\n", stderr);
        return 0;
    }

    if (diameter != 11) {
        fprintf(stderr,
                "BFS diameter mismatch: got %u, expected 11\n",
                diameter);

        free(table);
        free(exact_distance);
        return 0;
    }

    if (!test_heuristic_admissibility(exact_distance)) {
        free(table);
        free(exact_distance);
        return 0;
    }

    free(table);

    printf("[7/%u] IDA* known-distance solutions\n", test_count);
    if (!test_ida_star_known_distances()) {
        free(exact_distance);
        return 0;
    }

    if (exhaustive_ida) {
        puts("[8/8] H3 exhaustive IDA*/BFS distance equivalence");

        if (!test_ida_star_full(exact_distance)) {
            free(exact_distance);
            return 0;
        }
    }

    free(exact_distance);

    puts("all tests passed");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        if (!self_test(0, 0)) {
            fputs("self-test failed\n", stderr);
            return 1;
        }

        return output_failed();
    }

    if (argc == 2 && !strcmp(argv[1], "--full-test")) {
        if (!self_test(1, 1)) {
            fputs("full test failed\n", stderr);
            return 1;
        }

        return output_failed();
    }

    state_t state;

    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "my_solver");
        return 2;
    }

    search_state_t search = encode_search_state(&state);
    uint8_t solution[MAX_SOLUTION_LENGTH];
    uint8_t solution_length;

    if (!ida_star(search, solution, &solution_length)) {
        fputs("IDA* search failed\n", stderr);
        return 1;
    }

    const char *separator = "";

    for (uint8_t i = 0; i < solution_length; ++i) {
        uint8_t move = solution[i];
        printf("%s%s", separator, move_names[move]);
        separator = " ";
    }

    putchar('\n');
    return output_failed();
}
