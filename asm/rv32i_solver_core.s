.data
move_face:
    .byte 0, 0, 0, 1, 1, 1, 2, 2, 2
move_turns:
    .byte 1, 2, 3, 1, 2, 3, 1, 2, 3

.align 1
current_start:
    .half 0
    .half 0

parsed_permutation:
    .zero 7
parsed_permutation_end:
parsed_orientation:
    .zero 7

solution:
    .zero 11

.align 1
ida_frames:
    .zero 72

.text
# Find an optimal solution using a fixed 12-frame IDA* stack.
#   a0 = permutation rank
#   a1 = orientation rank
#   a2 = solution buffer
# Returns a0 = success and a1 = solution length.
ida_star:
    addi s0, a0, 0
    addi s1, a1, 0
    addi s7, a2, 0

    la t0, perm_dist
    add t0, t0, s0
    lbu t1, 0(t0)
    la t0, ori_dist
    add t0, t0, s1
    lbu t2, 0(t0)
    sltu t3, t1, t2
    beq t3, zero, initial_heuristic_ready
    addi t1, t2, 0

initial_heuristic_ready:
    addi s2, t1, 0
    or t0, s0, s1
    bne t0, zero, ida_search_initialize

    addi a0, zero, 1
    addi a1, zero, 0
    jalr zero, ra, 0

ida_search_initialize:
    la s5, ida_frames
    sh s0, 0(s5)
    sh s1, 2(s5)
    la s0, perm_dist
    la s1, ori_dist

ida_bound_loop:
    addi s3, zero, 0
    addi s4, zero, 255

    sb zero, 4(s5)
    addi t0, zero, 3
    sb t0, 5(s5)

ida_dfs_loop:
    lbu t0, 4(s5)
    addi t1, zero, 9
    bne t0, t1, ida_try_move

    beq s3, zero, ida_bound_finished
    addi s3, s3, -1
    addi s5, s5, -6
    jal zero, ida_dfs_loop

ida_try_move:
    addi t1, t0, 1
    sb t1, 4(s5)
    addi s6, t0, 0

    la t1, move_face
    add t1, t1, s6
    lbu t2, 0(t1)
    lbu t3, 5(s5)
    beq t2, t3, ida_dfs_loop

    lhu a0, 0(s5)
    lhu a1, 2(s5)
    addi a2, s6, 0
    addi a3, t2, 0
    jal s11, apply_search_move

    addi s8, a0, 0
    addi s9, a1, 0
    addi s10, s3, 1
    add t0, s7, s3
    sb s6, 0(t0)

    or t0, s8, s9
    beq t0, zero, ida_solution_found

    add t0, s0, s8
    lbu t1, 0(t0)
    add t0, s1, s9
    lbu t2, 0(t0)
    sltu t3, t1, t2
    beq t3, zero, ida_heuristic_ready
    addi t1, t2, 0

ida_heuristic_ready:
    add t1, t1, s10
    bltu s2, t1, ida_pruned

    addi s5, s5, 6
    sh s8, 0(s5)
    sh s9, 2(s5)
    sb zero, 4(s5)
    sb a3, 5(s5)
    addi s3, s10, 0
    jal zero, ida_dfs_loop

ida_pruned:
    bgeu t1, s4, ida_dfs_loop
    addi s4, t1, 0
    jal zero, ida_dfs_loop

ida_bound_finished:
    addi t0, zero, 255
    beq s4, t0, ida_failed
    addi s2, s4, 0
    sltiu t0, s2, 12
    bne t0, zero, ida_bound_loop

ida_failed:
    addi a0, zero, 0
    addi a1, zero, 0
    jalr zero, ra, 0

ida_solution_found:
    addi a0, zero, 1
    addi a1, s10, 0
    jalr zero, ra, 0

# Apply one HTM move to a compact state.
#   a0 = permutation rank, a1 = orientation rank
#   a2 = move, a3 = decoded face
# Returns through s11 with the next ranks in a0 and a1.
apply_search_move:
    la t0, move_turns
    add t0, t0, a2
    lbu t2, 0(t0)

    la t4, permutation
    la t5, orientation
    beq a3, zero, apply_rows_ready

    addi t3, zero, 1
    beq a3, t3, apply_face_one

    lui t3, 5
    addi t3, t3, -320
    add t4, t4, t3
    lui t3, 1
    addi t3, t3, -1180
    add t5, t5, t3
    jal zero, apply_rows_ready

apply_face_one:
    lui t3, 2
    addi t3, t3, 1888
    add t4, t4, t3
    addi t3, zero, 1458
    add t5, t5, t3

apply_rows_ready:
apply_turn_loop:
    slli t6, a0, 1
    add t6, t4, t6
    lhu a0, 0(t6)
    slli t6, a1, 1
    add t6, t5, t6
    lhu a1, 0(t6)
    addi t2, t2, -1
    bne t2, zero, apply_turn_loop
    jalr zero, s11, 0

# Replay and validate a returned path in the compact transition system.
#   a0 = start permutation, a1 = start orientation
#   a2 = solution buffer, a3 = solution length
# Returns a0 = 1 only when the path is valid and reaches solved.
replay_solution:
    addi s8, a2, 0
    addi s9, a3, 0
    addi s10, zero, 3

replay_loop:
    beq s9, zero, replay_finished
    lbu t0, 0(s8)
    sltiu t1, t0, 9
    beq t1, zero, replay_failed

    la t1, move_face
    add t1, t1, t0
    lbu t2, 0(t1)
    beq t2, s10, replay_failed
    addi s10, t2, 0

    addi a2, t0, 0
    addi a3, t2, 0
    jal s11, apply_search_move
    addi s8, s8, 1
    addi s9, s9, -1
    jal zero, replay_loop

replay_finished:
    or t0, a0, a1
    sltiu a0, t0, 1
    jalr zero, ra, 0

replay_failed:
    addi a0, zero, 0
    jalr zero, ra, 0

# Parse PPPPPPPOOOOOOO and return:
#   a0 = 0 on success, 1 on invalid input
#   a1 = permutation rank
#   a2 = orientation rank
parse_and_rank:
    addi t0, a0, 0
    la t1, parsed_permutation
    addi t2, zero, 0
    addi t3, zero, 7

parse_permutation_loop:
    lbu t4, 0(t0)
    addi t4, t4, -49
    sltiu t5, t4, 7
    beq t5, zero, parse_failed

    addi t6, zero, 1
    sll t6, t6, t4
    and a3, t2, t6
    bne a3, zero, parse_failed
    or t2, t2, t6

    sb t4, 0(t1)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t3, t3, -1
    bne t3, zero, parse_permutation_loop

    la t1, parsed_orientation
    addi t2, zero, 0
    addi t3, zero, 7

parse_orientation_loop:
    lbu t4, 0(t0)
    addi t4, t4, -49
    sltiu t5, t4, 3
    beq t5, zero, parse_failed

    sb t4, 0(t1)
    add t2, t2, t4
    addi t0, t0, 1
    addi t1, t1, 1
    addi t3, t3, -1
    bne t3, zero, parse_orientation_loop

    lbu t4, 0(t0)
    bne t4, zero, parse_failed

    beq t2, zero, orientation_sum_valid
    addi t3, zero, 3
    beq t2, t3, orientation_sum_valid
    addi t3, zero, 6
    beq t2, t3, orientation_sum_valid
    addi t3, zero, 9
    beq t2, t3, orientation_sum_valid
    addi t3, zero, 12
    bne t2, t3, parse_failed

orientation_sum_valid:
    la t0, parsed_permutation
    addi t1, zero, 0
    addi t2, zero, 0

rank_permutation_loop:
    lbu t3, 0(t0)
    addi t4, t0, 1
    la t5, parsed_permutation_end
    addi t6, zero, 0

count_smaller_loop:
    beq t4, t5, count_smaller_done
    lbu a3, 0(t4)
    sltu a3, a3, t3
    add t6, t6, a3
    addi t4, t4, 1
    jal zero, count_smaller_loop

count_smaller_done:
    beq t1, zero, rank_first_digit

    addi a3, zero, 1
    beq t1, a3, rank_times_six
    addi a3, zero, 2
    beq t1, a3, rank_times_five
    addi a3, zero, 3
    beq t1, a3, rank_times_four
    addi a3, zero, 4
    beq t1, a3, rank_times_three

    slli t2, t2, 1
    jal zero, add_rank_digit

rank_times_six:
    slli a3, t2, 2
    slli a4, t2, 1
    add t2, a3, a4
    jal zero, add_rank_digit

rank_times_five:
    slli a3, t2, 2
    add t2, a3, t2
    jal zero, add_rank_digit

rank_times_four:
    slli t2, t2, 2
    jal zero, add_rank_digit

rank_times_three:
    slli a3, t2, 1
    add t2, a3, t2
    jal zero, add_rank_digit

rank_first_digit:
    addi t2, t6, 0
    jal zero, rank_digit_done

add_rank_digit:
    add t2, t2, t6

rank_digit_done:
    addi t0, t0, 1
    addi t1, t1, 1
    addi a3, zero, 6
    bne t1, a3, rank_permutation_loop

    addi a1, t2, 0
    la t0, parsed_orientation
    addi t1, zero, 6
    addi t2, zero, 0

rank_orientation_loop:
    lbu t3, 0(t0)
    slli t4, t2, 1
    add t2, t4, t2
    add t2, t2, t3
    addi t0, t0, 1
    addi t1, t1, -1
    bne t1, zero, rank_orientation_loop

    addi a2, t2, 0
    addi a0, zero, 0
    jalr zero, ra, 0

parse_failed:
    addi a0, zero, 1
    jalr zero, ra, 0
