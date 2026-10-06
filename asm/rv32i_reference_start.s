.section .text.start
.globl _start
_start:
    la sp, reference_stack_end
    jal ra, main
    sltiu t6, a0, 1
    addi a7, zero, 10
    ecall

.section .bss
.align 4
reference_stack:
    .zero 512
reference_stack_end:
