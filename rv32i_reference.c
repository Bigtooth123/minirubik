#include <stdint.h>

#if !defined(RV32I_TARGET) && !defined(RV32I_TARGET_TESTS)
#include <stdio.h>
#endif

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    FACES = 3,
    MOVES = 9,
    MAX_SOLUTION_LENGTH = 11,
    IDA_STACK_DEPTH = MAX_SOLUTION_LENGTH + 1
};

typedef struct {
    uint8_t p[CUBIES];
    uint8_t o[CUBIES];
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

#include "static_tables.h"

static const uint8_t move_face[MOVES] = {
    0, 0, 0, 1, 1, 1, 2, 2, 2
};

static const uint8_t move_turns[MOVES] = {
    1, 2, 3, 1, 2, 3, 1, 2, 3
};

static const uint16_t *const permutation_face[FACES] = {
    permutation[0], permutation[1], permutation[2]
};

static const uint16_t *const orientation_face[FACES] = {
    orientation[0], orientation[1], orientation[2]
};

static int valid(const state_t *state)
{
    uint8_t sum = 0;

    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3U)
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

    for (uint8_t i = 0; i < 6U; ++i)
        o = (uint16_t) ((o << 1U) + o + state->o[i]);

    return o;
}

static search_state_t encode_search_state(const state_t *state)
{
    search_state_t result;

    result.p = rank_permutation(state);
    result.o = rank_orientation(state);
    return result;
}

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

static uint8_t heuristic(search_state_t state)
{
    uint8_t hp = perm_dist[state.p];
    uint8_t ho = ori_dist[state.o];

    return hp > ho ? hp : ho;
}

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
            uint8_t next_depth = (uint8_t) (depth + 1U);

            next = apply_search_move(next, move);
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

static int verify_solution(search_state_t state, const uint8_t *solution,
                           uint8_t solution_length)
{
    for (uint8_t i = 0; i < solution_length; ++i) {
        uint8_t move = solution[i];

        if (move >= MOVES)
            return 0;

        if (i != 0 && move_face[solution[i - 1U]] == move_face[move])
            return 0;

        state = apply_search_move(state, move);
    }

    return state.p == 0 && state.o == 0;
}

#if defined(RV32I_TARGET) || defined(RV32I_TARGET_TESTS)
static int run_case(const char *input, uint8_t expected_length)
{
    state_t state;
    uint8_t solution[MAX_SOLUTION_LENGTH];
    uint8_t solution_length;

    if (!parse_state(input, &state))
        return 1;

    search_state_t start = encode_search_state(&state);

    if (!ida_star(start, solution, &solution_length))
        return 2;

    if (!verify_solution(start, solution, solution_length))
        return 3;

    if (expected_length <= MAX_SOLUTION_LENGTH &&
        solution_length != expected_length)
        return 4;

    return 0;
}
#endif

#if defined(RV32I_TARGET_TESTS)

int main(void)
{
    enum { TARGET_TEST_CASES = 3 };
    static const struct {
        const char *input;
        uint8_t expected_length;
    } cases[TARGET_TEST_CASES] = {
        {"12345671111111", 0},
        {"25314672313211", 1},
        {"21345671111111", 11},
    };

    for (uint8_t i = 0; i < TARGET_TEST_CASES; ++i) {
        int status = run_case(cases[i].input, cases[i].expected_length);

        if (status != 0)
            return status;
    }

    return 0;
}

#elif defined(RV32I_TARGET)

#ifndef RV32I_CUBE_STATE
#define RV32I_CUBE_STATE "21345671111111"
#endif

#ifndef RV32I_EXPECTED_LENGTH
#define RV32I_EXPECTED_LENGTH 255U
#endif

int main(void)
{
    static const char input[] = RV32I_CUBE_STATE;

    return run_case(input, RV32I_EXPECTED_LENGTH);
}

#else

static const char *const move_names[MOVES] = {
    "R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'"
};

static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

int main(int argc, char **argv)
{
    state_t state;
    uint8_t solution[MAX_SOLUTION_LENGTH];
    uint8_t solution_length;

    if (argc != 2 || !parse_state(argv[1], &state)) {
        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "rv32i_reference");
        return 2;
    }

    search_state_t start = encode_search_state(&state);

    if (!ida_star(start, solution, &solution_length) ||
        !verify_solution(start, solution, solution_length)) {
        fputs("IDA* search failed\n", stderr);
        return 1;
    }

    const char *separator = "";

    for (uint8_t i = 0; i < solution_length; ++i) {
        printf("%s%s", separator, move_names[solution[i]]);
        separator = " ";
    }

    putchar('\n');
    return output_failed();
}

#endif
