.text
    main:
    li t0, 100000000
    loop:
    addi t0, t0, -1
    bne t0, x0, loop
    li a7, 10
    ecall