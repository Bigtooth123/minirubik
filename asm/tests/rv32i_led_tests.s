.data
.align 2

led_test_input:
    .string "21345671111111"
led_test_solution_length:
    .byte 0
led_test_r_colors:
    .byte 0, 2, 0, 2
    .byte 1, 1, 1, 1
    .byte 2, 5, 2, 5
    .byte 3, 3, 3, 3
    .byte 0, 4, 0, 4
    .byte 5, 4, 5, 4

.align 2
led_test_solution_pointer:
    .word 0
led_test_solution_remaining:
    .word 0
led_test_framebuffer:
    .zero 3500

.text
.globl main
main:
    la a0, led_test_input
    jal ra, parse_and_rank
    bne a0, zero, led_test_failed

    la t0, current_start
    sh a1, 0(t0)
    sh a2, 2(t0)
    addi a0, a1, 0
    addi a1, a2, 0
    la a2, solution
    jal ra, ida_star
    beq a0, zero, led_test_failed
    addi t0, zero, 11
    bne a1, t0, led_test_failed
    la t0, led_test_solution_length
    sb a1, 0(t0)

    addi a3, a1, 0
    la t0, current_start
    lhu a0, 0(t0)
    lhu a1, 2(t0)
    la a2, solution
    jal ra, replay_solution
    beq a0, zero, led_test_failed

    jal ra, render_initialize
    la t0, led_test_solution_pointer
    la t1, solution
    sw t1, 0(t0)
    la t0, led_test_solution_remaining
    la t1, led_test_solution_length
    lbu t1, 0(t1)
    sw t1, 0(t0)

led_test_apply_loop:
    la t0, led_test_solution_remaining
    lw t1, 0(t0)
    beq t1, zero, led_test_state_finished
    addi t1, t1, -1
    sw t1, 0(t0)
    la t0, led_test_solution_pointer
    lw t1, 0(t0)
    lbu a0, 0(t1)
    addi t1, t1, 1
    sw t1, 0(t0)
    jal ra, render_apply_move
    jal zero, led_test_apply_loop

led_test_state_finished:
    la t0, render_permutation
    la t1, render_orientation
    addi t2, zero, 0
    addi t3, zero, 7

led_test_state_loop:
    lbu t4, 0(t0)
    bne t4, t2, led_test_failed
    lbu t4, 0(t1)
    bne t4, zero, led_test_failed
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, 1
    addi t3, t3, -1
    bne t3, zero, led_test_state_loop

    la a0, led_test_framebuffer
    addi a1, zero, 35
    addi a2, zero, 25
    jal ra, render_cube

    la s0, facelet_x
    la s1, facelet_y
    addi s2, zero, 0
    addi s3, zero, 4
    addi s4, zero, 24

led_test_facelet_loop:
    lbu t0, 0(s0)
    lbu t1, 0(s1)
    slli t2, t1, 7
    slli t3, t1, 3
    add t2, t2, t3
    slli t3, t1, 2
    add t2, t2, t3
    slli t0, t0, 2
    add t2, t2, t0
    la t3, led_test_framebuffer
    add t3, t3, t2
    lw t4, 0(t3)

    slli t0, s2, 2
    la t1, led_palette
    add t1, t1, t0
    lw t0, 0(t1)
    bne t4, t0, led_test_failed

    addi s0, s0, 1
    addi s1, s1, 1
    addi s3, s3, -1
    bne s3, zero, led_test_facelet_group_ready
    addi s2, s2, 1
    addi s3, zero, 4

led_test_facelet_group_ready:
    addi s4, s4, -1
    bne s4, zero, led_test_facelet_loop

    la t0, led_test_framebuffer
    lw t1, 0(t0)
    bne t1, zero, led_test_failed
    lw t1, 1012(t0)
    bne t1, zero, led_test_failed
    lui t2, 1
    addi t2, t2, -600
    add t2, t0, t2
    lw t1, 0(t2)
    bne t1, zero, led_test_failed

    addi a0, zero, 0
    jal ra, render_apply_move
    la a0, led_test_framebuffer
    addi a1, zero, 35
    addi a2, zero, 25
    jal ra, render_cube

    la s0, facelet_x
    la s1, facelet_y
    la s2, led_test_r_colors
    addi s3, zero, 24

led_test_r_facelet_loop:
    lbu t0, 0(s0)
    lbu t1, 0(s1)
    slli t2, t1, 7
    slli t3, t1, 3
    add t2, t2, t3
    slli t3, t1, 2
    add t2, t2, t3
    slli t0, t0, 2
    add t2, t2, t0
    la t3, led_test_framebuffer
    add t3, t3, t2
    lw t4, 0(t3)

    lbu t0, 0(s2)
    slli t0, t0, 2
    la t1, led_palette
    add t1, t1, t0
    lw t0, 0(t1)
    bne t4, t0, led_test_failed

    addi s0, s0, 1
    addi s1, s1, 1
    addi s2, s2, 1
    addi s3, s3, -1
    bne s3, zero, led_test_r_facelet_loop

    addi t5, zero, 11
    addi t6, zero, 1
    jal zero, led_test_exit

led_test_failed:
    addi t6, zero, 0

led_test_exit:
    addi a7, zero, 10
    ecall
