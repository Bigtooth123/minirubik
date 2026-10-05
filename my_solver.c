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
    MOVES = 9
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

typedef struct {
    uint16_t p;
    uint16_t o;
} search_state_t;

static const char *const move_names[MOVES] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};

static const uint8_t inverse_move[MOVES] = {
    2, 1, 0, 5, 4, 3, 8, 7, 6
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
 * Factored quarter-turn transition tables.
 *
 * These already existed conceptually in the baseline. They are now
 * independent of the complete BFS so they can later be reused by IDA*.
 */
static uint16_t permutation[FACES][PERMUTATIONS];
static uint16_t orientation[FACES][ORIENTATIONS];

/*
 * New heuristic tables.
 *
 * No packing: one byte per entry.
 */
static uint8_t perm_dist[PERMUTATIONS];
static uint8_t ori_dist[ORIENTATIONS];

static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        result.o[i] = (uint8_t) ((state.o[from] + twist[face][i]) % 3U);
    }

    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t face = (uint8_t) (move / 3U);
    uint8_t turns = (uint8_t) (move % 3U + 1U);

    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, face);

    return state;
}

static uint32_t rank_state(const state_t *state)
{
    uint32_t p = 0, o = 0;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;

        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j) {
            if (state->p[j] < state->p[i])
                ++smaller;
        }

        p = p * (CUBIES - i) + smaller;
    }

    for (uint8_t i = 0; i < 6; ++i)
        o = o * 3U + state->o[i];

    return p * ORIENTATIONS + o;
}

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

        for (uint8_t j = q; j + 1U < CUBIES - i; ++j)
            available[j] = available[j + 1U];

        if (i < 5)
            f /= 6U - i;
    }

    for (uint8_t i = 6; i-- > 0;) {
        state->o[i] = (uint8_t) (o % 3U);
        sum = (uint8_t) (sum + state->o[i]);
        o /= 3U;
    }

    state->o[6] = (uint8_t) ((3U - sum % 3U) % 3U);
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

    return sum % 3U == 0;
}

static int parse_state(const char *input, state_t *state)
{
    for (int i = 0; i < 14; ++i) {
        int limit = i < 7 ? 7 : 3;

        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;

        (i < 7 ? state->p : state->o)[i % 7] =
            (uint8_t) (input[i] - '1');
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
    return split_rank(rank_state(state));
}

/*
 * Generate the factored quarter-turn transition tables.
 */
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

static uint16_t apply_perm_move(uint16_t p, uint8_t move)
{
    uint8_t face = (uint8_t) (move / 3U);
    uint8_t turns = (uint8_t) (move % 3U + 1U);

    for (uint8_t i = 0; i < turns; ++i)
        p = permutation[face][p];

    return p;
}

static uint16_t apply_ori_move(uint16_t o, uint8_t move)
{
    uint8_t face = (uint8_t) (move / 3U);
    uint8_t turns = (uint8_t) (move % 3U + 1U);

    for (uint8_t i = 0; i < turns; ++i)
        o = orientation[face][o];

    return o;
}

static search_state_t apply_search_move(search_state_t state, uint8_t move)
{
    state.p = apply_perm_move(state.p, move);
    state.o = apply_ori_move(state.o, move);
    return state;
}

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

static uint8_t heuristic(search_state_t state)
{
    uint8_t hp = perm_dist[state.p];
    uint8_t ho = ori_dist[state.o];

    return hp > ho ? hp : ho;
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

static int self_test(int exhaustive_transitions)
{
    uint8_t diameter;

    puts("[1/6] move inverse");
    if (!test_move_inverse())
        return 0;

    puts("[2/6] rank/unrank");
    if (!test_rank_roundtrip())
        return 0;

    puts("[3/6] transition bounds");
    if (!test_transition_bounds())
        return 0;

    if (exhaustive_transitions)
        puts("[4/6] exhaustive transition equivalence");
    else
        puts("[4/6] sampled transition equivalence");

    if (!test_transition_equivalence(exhaustive_transitions))
        return 0;

    puts("[5/6] H2 heuristic table sanity");
    if (!test_heuristic_tables())
        return 0;

    puts("[6/6] H1 heuristic admissibility");

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
    free(exact_distance);

    puts("all tests passed");
    return 1;
}

int main(int argc, char **argv)
{
    /*
     * Commit 2 still generates transitions and heuristics at runtime
     * on the native host.
     *
     * A later commit will turn these into host-generated const tables.
     */
    build_transition_tables();
    build_heuristic_tables();

    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        if (!self_test(0)) {
            fputs("self-test failed\n", stderr);
            return 1;
        }

        return output_failed();
    }

    if (argc == 2 && !strcmp(argv[1], "--full-test")) {
        if (!self_test(1)) {
            fputs("full test failed\n", stderr);
            return 1;
        }

        return output_failed();
    }

    state_t state;

    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }

    /*
     * The query path is deliberately still the baseline solver.
     *
     * IDA* is introduced in the next commit.
     */
    uint8_t diameter;
    uint8_t *table = build_table(&diameter, NULL);

    if (!table) {
        fputs("could not build complete state table\n", stderr);
        return 1;
    }

    const char *separator = "";

    for (uint32_t rank = rank_state(&state);
         rank;
         rank = rank_state(&state)) {

        uint8_t move = table[rank];

        printf("%s%s", separator, move_names[move]);
        separator = " ";

        state = apply_move(state, move);
    }

    putchar('\n');

    free(table);
    return output_failed();
}