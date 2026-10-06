.data
.align 2

# The CLI has no I/O devices.  This RAM buffer occupies the default data base
# and supplies the same symbols as a 35 by 25 GUI LED Matrix for a smoke test.
led_test_mmio:
    .zero 3500

.equ LED_MATRIX_0_BASE, 0x10000000
.equ LED_MATRIX_0_WIDTH, 35
.equ LED_MATRIX_0_HEIGHT, 25
