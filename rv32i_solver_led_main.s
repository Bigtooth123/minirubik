.data
.align 2

input_state:
    .string "21345671111111"
solution_length:
    .byte 0

.text
.globl main
main:
    la a0, input_state
    jal ra, parse_and_rank
    bne a0, zero, program_failed

    la t0, current_start
    sh a1, 0(t0)
    sh a2, 2(t0)
    addi a0, a1, 0
    addi a1, a2, 0
    la a2, solution
    jal ra, ida_star
    beq a0, zero, program_failed

    la t0, solution_length
    sb a1, 0(t0)
    addi a3, a1, 0
    la t0, current_start
    lhu a0, 0(t0)
    lhu a1, 2(t0)
    la a2, solution
    jal ra, replay_solution
    beq a0, zero, program_failed

    la a0, solution
    la t0, solution_length
    lbu a1, 0(t0)
    li a2, LED_MATRIX_0_BASE
    li a3, LED_MATRIX_0_WIDTH
    li a4, LED_MATRIX_0_HEIGHT
    jal ra, render_solution

    la t0, solution_length
    lbu t5, 0(t0)
    addi t6, zero, 1
    jal zero, exit_program

program_failed:
    addi t6, zero, 0

exit_program:
    addi a7, zero, 10
    ecall
