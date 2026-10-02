# Writes BYTES bytes of guest memory with word stores, then spins so the
# host process can be sampled while all written bytes are resident.
# BYTES is patched by run_memory.ps1; 4 is the control.
.equ BYTES, 4

.text
main:
    li   t0, 0x10010000        # start of the written region
    li   t1, BYTES
    add  t1, t0, t1            # one past the end
    li   t2, -1                # nonzero pattern, every byte written
fill:
    sw   t2, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, fill
spin:
    j    spin                  # ended by Ripes --timeout
