.data
buf:

.text
main:
    la      t0, buf
    li      t1, 1000000
loop:
    sw      t1, 0(t0)
    addi    t1, t1, -1
    bne     t1, x0, loop
    li      a7, 10
    ecall    