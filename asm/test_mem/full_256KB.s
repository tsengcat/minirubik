.data
buf:

.text
main:
    la   t0, buf
    li   t1, 256000    #total 2.5kiB
    add  t1, t0, t1
    li   t2, 0x12345678
loop:
    sw   t2, 0(t0)
    addi t0, t0, 4
    bne  t0, t1, loop
    li   a7, 10
    ecall