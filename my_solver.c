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

/*
 * Compact representation used by the new solver.
 *
 * p: permutation rank, 0..5039
 * o: orientation rank, 0..728
 */
typedef struct {
    uint16_t p;
    uint16_t o;
} search_state_t;

static const char *const move_names[MOVES] = {
    "R", "R2", "R'",
    "B", "B2", "B'",
    "D", "D2", "D'"
};

static const uint8_t inverse_move[MOVES] = {
    2, 1, 0,
    5, 4, 3,
    8, 7, 6
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
 * Quarter-turn transition tables.
 *
 * permutation[face][p]:
 *     permutation rank after one clockwise quarter turn
 *
 * orientation[face][o]:
 *     orientation rank after one clockwise quarter turn
 */
static uint16_t permutation[FACES][PERMUTATIONS];
static uint16_t orientation[FACES][ORIENTATIONS];

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

        (i < 7 ? state->p : state->o)[i % 7] = (uint8_t) (input[i] - '1');
    }

    return input[14] == '\0' && valid(state);
}

/*
 * Convert the original state_t into the new factored representation.
 */
static search_state_t encode_search_state(const state_t *state)
{
    uint32_t rank = rank_state(state);

    search_state_t result;
    result.p = (uint16_t) (rank / ORIENTATIONS);
    result.o = (uint16_t) (rank % ORIENTATIONS);

    return result;
}

/*
 * Build the two factored quarter-turn transition tables.
 *
 * This is still runtime generation for the C prototype.
 * A later commit will move these tables to host-side precomputation.
 */
static void build_transition_tables(void)
{
    state_t state;

    /*
     * Permutation transition table.
     *
     * Orientation is set to solved while each of the 5040
     * permutation ranks is tested.
     */
    for (uint16_t p = 0; p < PERMUTATIONS; ++p) {
        unrank_state((uint32_t) p * ORIENTATIONS, &state);

        for (uint8_t face = 0; face < FACES; ++face) {
            state_t next = quarter_turn(state, face);
            permutation[face][p] =
                (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }

    /*
     * Orientation transition table.
     *
     * Permutation is solved while each of the 729
     * orientation ranks is tested.
     */
    for (uint16_t o = 0; o < ORIENTATIONS; ++o) {
        unrank_state(o, &state);

        for (uint8_t face = 0; face < FACES; ++face) {
            state_t next = quarter_turn(state, face);
            orientation[face][o] =
                (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
}

/*
 * Apply one of the nine HTM moves directly to (p,o).
 *
 * move % 3:
 *   0 -> quarter turn
 *   1 -> half turn
 *   2 -> inverse quarter turn
 *
 * Only quarter-turn tables are stored. R2 and R' are generated
 * by repeating the quarter-turn lookup.
 */
static search_state_t apply_search_move(search_state_t state, uint8_t move)
{
    uint8_t face = (uint8_t) (move / 3U);
    uint8_t turns = (uint8_t) (move % 3U + 1U);

    for (uint8_t i = 0; i < turns; ++i) {
        state.p = permutation[face][state.p];
        state.o = orientation[face][state.o];
    }

    return state;
}

static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

/*
 * Existing baseline-style test:
 * move followed by its inverse must return to solved.
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

/*
 * Check rank/unrank over the complete state space.
 */
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

/*
 * Check that every table entry remains inside its legal domain.
 */
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

/*
 * Compare the new factored transition implementation against
 * the original state_t implementation.
 *
 * exhaustive == 0:
 *     test a regularly spaced subset, useful during development
 *
 * exhaustive != 0:
 *     test all 3,674,160 states and all 9 moves
 */
static int test_transition_equivalence(int exhaustive)
{
    state_t state;

    uint32_t step = exhaustive ? 1U : 997U;

    for (uint32_t rank = 0; rank < STATES; rank += step) {
        unrank_state(rank, &state);

        search_state_t compact = encode_search_state(&state);

        for (uint8_t move = 0; move < MOVES; ++move) {
            state_t expected_state = apply_move(state, move);
            uint32_t expected_rank = rank_state(&expected_state);

            search_state_t expected;
            expected.p =
                (uint16_t) (expected_rank / ORIENTATIONS);
            expected.o =
                (uint16_t) (expected_rank % ORIENTATIONS);

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

static int self_test(int exhaustive)
{
    puts("[1/4] move inverse");

    if (!test_move_inverse())
        return 0;

    puts("[2/4] rank/unrank");

    if (!test_rank_roundtrip())
        return 0;

    puts("[3/4] transition bounds");

    if (!test_transition_bounds())
        return 0;

    if (exhaustive)
        puts("[4/4] exhaustive transition equivalence");
    else
        puts("[4/4] sampled transition equivalence");

    if (!test_transition_equivalence(exhaustive))
        return 0;

    puts("all tests passed");
    return 1;
}

int main(int argc, char **argv)
{
    build_transition_tables();

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
                argc > 0 && argv[0] ? argv[0] : "my_solver");
        return 2;
    }

    search_state_t search = encode_search_state(&state);

    /*
     * For now, print the internal factored representation
     * so that the input path can be tested.
     */
    printf("p=%u o=%u\n", search.p, search.o);

    return output_failed();
}