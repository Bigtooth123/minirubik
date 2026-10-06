.data
.align 2

# RGB colors for U, L, F, R, B, and D.
led_palette:
    .word 0x00ffffff
    .word 0x00ff8000
    .word 0x0000cc44
    .word 0x00ff2020
    .word 0x002040ff
    .word 0x00ffff00

# Three solved-face colors for cubies 0 through 7.  The first entry is the
# U/D sticker; the other two follow the standard corner orientation order.
cubie_sticker_colors:
    .byte 0, 2, 1
    .byte 0, 3, 2
    .byte 5, 2, 3
    .byte 5, 1, 2
    .byte 0, 4, 3
    .byte 5, 3, 4
    .byte 5, 4, 1
    .byte 0, 1, 4

# Facelet order is U, L, F, R, B, D, four facelets per face.
facelet_position:
    .byte 7, 4, 0, 1
    .byte 7, 0, 6, 3
    .byte 0, 1, 3, 2
    .byte 1, 4, 2, 5
    .byte 4, 7, 5, 6
    .byte 3, 2, 6, 5
facelet_slot:
    .byte 0, 0, 0, 0
    .byte 1, 2, 2, 1
    .byte 1, 2, 2, 1
    .byte 1, 2, 2, 1
    .byte 1, 2, 2, 1
    .byte 0, 0, 0, 0
facelet_x:
    .byte 9, 13, 9, 13
    .byte 0, 4, 0, 4
    .byte 9, 13, 9, 13
    .byte 18, 22, 18, 22
    .byte 27, 31, 27, 31
    .byte 9, 13, 9, 13
facelet_y:
    .byte 0, 0, 3, 3
    .byte 7, 7, 10, 10
    .byte 7, 7, 10, 10
    .byte 7, 7, 10, 10
    .byte 7, 7, 10, 10
    .byte 14, 14, 17, 17

# Full-state quarter-turn tables for the seven moving corner positions.
visual_source:
    .byte 1, 4, 2, 0, 3, 5, 6
    .byte 0, 1, 2, 4, 5, 6, 3
    .byte 0, 2, 5, 3, 1, 4, 6
visual_twist:
    .byte 1, 2, 0, 2, 1, 0, 0
    .byte 0, 0, 0, 1, 2, 1, 2
    .byte 0, 0, 0, 0, 0, 0, 0

render_permutation:
    .zero 7
render_orientation:
    .zero 7
render_next_permutation:
    .zero 7
render_next_orientation:
    .zero 7

.align 2
render_saved_ra:
    .word 0
render_solution_pointer:
    .word 0
render_solution_remaining:
    .word 0
render_led_base:
    .word 0
render_led_width:
    .word 0
render_led_height:
    .word 0
render_delay_count:
    .word 200000

.text
# Copy the parsed input into the independent visualization state.
render_initialize:
    la t0, parsed_permutation
    la t1, parsed_orientation
    la t2, render_permutation
    la t3, render_orientation
    addi t4, zero, 7

render_initialize_loop:
    lbu t5, 0(t0)
    sb t5, 0(t2)
    lbu t5, 0(t1)
    sb t5, 0(t3)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, 1
    addi t3, t3, 1
    addi t4, t4, -1
    bne t4, zero, render_initialize_loop
    jalr zero, ra, 0

# Apply one encoded HTM move to the visualization state.
#   a0 = move in the range 0 through 8
render_apply_move:
    la t0, move_face
    add t0, t0, a0
    lbu t1, 0(t0)
    la t0, move_turns
    add t0, t0, a0
    lbu t2, 0(t0)

    slli t3, t1, 3
    sub t3, t3, t1
    la t4, visual_source
    add t4, t4, t3
    la t5, visual_twist
    add t5, t5, t3

render_apply_turn:
    la s0, render_permutation
    la s1, render_orientation
    la s2, render_next_permutation
    la s3, render_next_orientation
    addi s4, t4, 0
    addi s5, t5, 0
    addi s6, zero, 7

render_apply_position:
    lbu t0, 0(s4)
    add t1, s0, t0
    lbu t1, 0(t1)
    sb t1, 0(s2)

    add t1, s1, t0
    lbu t1, 0(t1)
    lbu t3, 0(s5)
    add t1, t1, t3
    sltiu t3, t1, 3
    bne t3, zero, render_orientation_ready
    addi t1, t1, -3

render_orientation_ready:
    sb t1, 0(s3)
    addi s2, s2, 1
    addi s3, s3, 1
    addi s4, s4, 1
    addi s5, s5, 1
    addi s6, s6, -1
    bne s6, zero, render_apply_position

    la s0, render_permutation
    la s1, render_orientation
    la s2, render_next_permutation
    la s3, render_next_orientation
    addi s6, zero, 7

render_copy_position:
    lbu t0, 0(s2)
    sb t0, 0(s0)
    lbu t0, 0(s3)
    sb t0, 0(s1)
    addi s0, s0, 1
    addi s1, s1, 1
    addi s2, s2, 1
    addi s3, s3, 1
    addi s6, s6, -1
    bne s6, zero, render_copy_position

    addi t2, t2, -1
    bne t2, zero, render_apply_turn
    jalr zero, ra, 0

# Draw the visualization state as a 35 by 20 unfolded net.  Each facelet is a
# 4 by 3 block, and the remaining rows of a 35 by 25 matrix stay black.
#   a0 = LED framebuffer base, a1 = width, a2 = height
render_cube:
    addi s0, a0, 0
    slli s1, a1, 2

    addi t0, a0, 0
    addi t1, a2, 0

render_clear_row:
    beq t1, zero, render_clear_done
    addi t2, a1, 0

render_clear_pixel:
    beq t2, zero, render_clear_next_row
    sw zero, 0(t0)
    addi t0, t0, 4
    addi t2, t2, -1
    jal zero, render_clear_pixel

render_clear_next_row:
    addi t1, t1, -1
    jal zero, render_clear_row

render_clear_done:
    la s2, facelet_position
    la s3, facelet_slot
    la s4, facelet_x
    la s5, facelet_y
    addi s6, zero, 24

render_facelet_loop:
    lbu t0, 0(s2)
    lbu t1, 0(s3)
    beq t0, zero, render_fixed_corner

    addi t2, t0, -1
    la t3, render_permutation
    add t3, t3, t2
    lbu t4, 0(t3)
    addi t4, t4, 1
    la t3, render_orientation
    add t3, t3, t2
    lbu t5, 0(t3)
    jal zero, render_sticker_index

render_fixed_corner:
    addi t4, zero, 0
    addi t5, zero, 0

render_sticker_index:
    add t1, t1, t5
    addi t6, zero, 3
    bltu t1, t6, render_sticker_index_ready
    addi t1, t1, -3

render_sticker_index_ready:
    slli t2, t4, 1
    add t2, t2, t4
    add t2, t2, t1
    la t3, cubie_sticker_colors
    add t3, t3, t2
    lbu t2, 0(t3)
    slli t2, t2, 2
    la t3, led_palette
    add t3, t3, t2
    lw a7, 0(t3)

    lbu t4, 0(s4)
    lbu t5, 0(s5)
    addi t0, s0, 0

render_find_row:
    beq t5, zero, render_row_ready
    add t0, t0, s1
    addi t5, t5, -1
    jal zero, render_find_row

render_row_ready:
    slli t4, t4, 2
    add t0, t0, t4

    sw a7, 0(t0)
    sw a7, 4(t0)
    sw a7, 8(t0)
    sw a7, 12(t0)
    add t0, t0, s1
    sw a7, 0(t0)
    sw a7, 4(t0)
    sw a7, 8(t0)
    sw a7, 12(t0)
    add t0, t0, s1
    sw a7, 0(t0)
    sw a7, 4(t0)
    sw a7, 8(t0)
    sw a7, 12(t0)

    addi s2, s2, 1
    addi s3, s3, 1
    addi s4, s4, 1
    addi s5, s5, 1
    addi s6, s6, -1
    bne s6, zero, render_facelet_loop
    jalr zero, ra, 0

render_delay:
    la t0, render_delay_count
    lw t0, 0(t0)

render_delay_loop:
    beq t0, zero, render_delay_done
    addi t0, t0, -1
    jal zero, render_delay_loop

render_delay_done:
    jalr zero, ra, 0

# Render the parsed input, then apply and render every move returned by IDA*.
#   a0 = solution, a1 = length, a2 = framebuffer base
#   a3 = framebuffer width, a4 = framebuffer height
render_solution:
    la t0, render_saved_ra
    sw ra, 0(t0)
    la t0, render_solution_pointer
    sw a0, 0(t0)
    la t0, render_solution_remaining
    sw a1, 0(t0)
    la t0, render_led_base
    sw a2, 0(t0)
    la t0, render_led_width
    sw a3, 0(t0)
    la t0, render_led_height
    sw a4, 0(t0)

    jal ra, render_initialize
    la t0, render_led_base
    lw a0, 0(t0)
    la t0, render_led_width
    lw a1, 0(t0)
    la t0, render_led_height
    lw a2, 0(t0)
    jal ra, render_cube
    jal ra, render_delay

render_solution_loop:
    la t0, render_solution_remaining
    lw t1, 0(t0)
    beq t1, zero, render_solution_done
    addi t1, t1, -1
    sw t1, 0(t0)

    la t0, render_solution_pointer
    lw t1, 0(t0)
    lbu a0, 0(t1)
    addi t1, t1, 1
    sw t1, 0(t0)
    jal ra, render_apply_move

    la t0, render_led_base
    lw a0, 0(t0)
    la t0, render_led_width
    lw a1, 0(t0)
    la t0, render_led_height
    lw a2, 0(t0)
    jal ra, render_cube
    jal ra, render_delay
    jal zero, render_solution_loop

render_solution_done:
    la t0, render_saved_ra
    lw ra, 0(t0)
    jalr zero, ra, 0
