/* ESP32-C3 ROM MEM_END entry. No SDK startup or operating system. */
.section .text.start,"ax",@progbits
.balign 4
.global _start
.type _start,@function
_start:
    /* C3 has no mie CSR; global interrupt gating is mstatus.MIE. */
    csrci mstatus, 8
    .option push
    .option norelax
    la gp, __global_pointer$
    la sp, __stack_top
    .option pop
    la t0, trap_entry
    ori t0, t0, 1
    csrw mtvec, t0
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:  fence.i
    call native_main
3:  j 3b
.size _start, .-_start

/* C3 mtvec is vectored-only and its BASE must be 256-byte aligned. */
.balign 256
trap_entry:
    la sp, __stack_top
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    call native_trap
4:  j 4b
