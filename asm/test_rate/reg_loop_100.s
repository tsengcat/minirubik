.text
    main:
    li t0, 100
    loop:
    addi t0, t0, -1
    bne t0, x0, loop
    li a7, 10
    ecall