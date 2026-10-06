.data
.align 2

test_solved:
    .string "12345671111111"
test_one_move:
    .string "25314672313211"
test_distance_11:
    .string "21345671111111"

.align 2
valid_cases:
    .word test_solved
    .half 0
    .half 0
    .byte 0
    .zero 3
    .word test_one_move
    .half 1104
    .half 426
    .byte 1
    .zero 3
    .word test_distance_11
    .half 720
    .half 0
    .byte 11
    .zero 3

invalid_short:
    .string "1234567111111"
invalid_long:
    .string "123456711111111"
invalid_cubie_low:
    .string "02345671111111"
invalid_cubie_high:
    .string "82345671111111"
invalid_orientation_low:
    .string "12345671111110"
invalid_orientation_high:
    .string "12345671111114"
invalid_character:
    .string "1234567111111a"
invalid_duplicate:
    .string "11345671111111"
invalid_orientation_sum:
    .string "12345671111112"

.align 2
invalid_cases:
    .word invalid_short
    .word invalid_long
    .word invalid_cubie_low
    .word invalid_cubie_high
    .word invalid_orientation_low
    .word invalid_orientation_high
    .word invalid_character
    .word invalid_duplicate
    .word invalid_orientation_sum

saved_test_cursor:
    .word 0
saved_test_remaining:
    .word 0

.text
.globl main
main:
    la s0, valid_cases
    addi s1, zero, 3

valid_test_loop:
    lw a0, 0(s0)
    jal ra, parse_and_rank
    bne a0, zero, test_failed

    lhu t0, 4(s0)
    bne a1, t0, test_failed
    lhu t0, 6(s0)
    bne a2, t0, test_failed

    la t0, current_start
    sh a1, 0(t0)
    sh a2, 2(t0)
    la t0, saved_test_cursor
    sw s0, 0(t0)
    la t0, saved_test_remaining
    sw s1, 0(t0)

    addi a0, a1, 0
    addi a1, a2, 0
    la a2, solution
    jal ra, ida_star

    la t0, saved_test_cursor
    lw s0, 0(t0)
    la t0, saved_test_remaining
    lw s1, 0(t0)
    beq a0, zero, test_failed
    lbu t0, 8(s0)
    bne a1, t0, test_failed

    addi a3, a1, 0
    la t0, current_start
    lhu a0, 0(t0)
    lhu a1, 2(t0)
    la a2, solution
    jal ra, replay_solution
    beq a0, zero, test_failed

    addi s0, s0, 12
    addi s1, s1, -1
    bne s1, zero, valid_test_loop

    la s0, invalid_cases
    addi s1, zero, 9

invalid_test_loop:
    lw a0, 0(s0)
    jal ra, parse_and_rank
    beq a0, zero, test_failed
    addi s0, s0, 4
    addi s1, s1, -1
    bne s1, zero, invalid_test_loop

    addi t6, zero, 1
    jal zero, test_exit

test_failed:
    addi t6, zero, 0

test_exit:
    addi a7, zero, 10
    ecall
