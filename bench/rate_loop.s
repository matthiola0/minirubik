# Retires about 3 * ITERS instructions (exact count from --iret), one store
# per iteration,
# then exits. ITERS is patched by run_rate.ps1.
.equ ITERS, 1000000

.text
main:
    li   t0, ITERS
    li   t1, 0x10010000
loop:
    sw   t0, 0(t1)
    addi t0, t0, -1
    bnez t0, loop
    li   a7, 10                # exit
    ecall
