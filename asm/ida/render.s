# ===========================================================================
# render.s：在 LED Matrix 上畫方塊的展開圖（只放進 GUI build，見 build.sh）
#
# Ripes 沒有 .if，CLI 也沒有 LED peripheral，引用 LED_MATRIX_0_* 會組譯失敗。
# 所以 renderer 整個放在這個檔案：ida.s 裡呼叫它的兩行行尾標著 RENDER，
# build.sh 產生 CLI build 時刪掉那兩行、也不接這個檔案。兩個 build 的差別
# 只有 renderer。
#
# 由 solver 的實際輸出驅動：
#   render_init：read_state 之後呼叫，從 input 字串建立 8 個角塊的狀態，畫一次
#   render_move：check_path 每重播一步呼叫一次，照 frame（x24）的 face、turn
#                轉 turn + 1 個四分之一圈，再畫一次
# 轉法用 solver.c 同一組 source、twist 表，所以最後畫回 solved，也等於用另一個
# 模型再確認一次 T5。
#
# 展開圖（reports/report.md 的 exterior unfold），每格 4 × 3 像素，面和面之間
# 留 1 像素黑色，共 35 × 20：
#          U
#       L  F  R  B
#          D
# 位置 p 的三格：k = 0 是 U/D 那一面，k = 1、2 依「從外面看順時針」排列。
# cubie c、twist t 放在位置 p 時，第 k 格是 c 的第 (k + t) mod 3 格顏色。
# 這個對應在 host 上用 3D 旋轉檢查過全部 441 種（面 × 位置 × cubie × twist）。
#
# 暫存器：只用 x1（返回位址）、x10、x11、x13-x16、x18-x23、x26-x31，
# 不碰 x3-x9、x12、x24、x25（ida.s 的全程暫存器和 check_path 的迴圈）。
# ===========================================================================
    .equ RENDER_DELAY, 300000       # 每畫一張之後空轉幾次（至少 1），可以自己調

    .data
    .align 4
rs_palette:                     # 面的顏色 0x00RRGGBB，順序 U R F D L B
    .word 0xFFFFFF, 0xFF0000, 0x00C000, 0xFFFF00, 0xFF8000, 0x0040FF
rs_src:                         # 位置 i 轉完之後的 cubie 從哪個位置來（solver.c 的 source，位置 0 固定）
    .byte 0, 2, 5, 3, 1, 4, 6, 7    # R
    .byte 0, 1, 2, 3, 5, 6, 7, 4    # B
    .byte 0, 1, 3, 6, 4, 2, 5, 7    # D
rs_tw:                          # 轉到位置 i 時 twist 要加多少（solver.c 的 twist）
    .byte 0, 1, 2, 0, 2, 1, 0, 0    # R
    .byte 0, 0, 0, 0, 1, 2, 1, 2    # B
    .byte 0, 0, 0, 0, 0, 0, 0, 0    # D
rs_colors:                      # cubie c 在原位時三格的面（U/D 那格，再依順時針），補一個 0
    .byte 0, 2, 4, 0    # 0: U F L
    .byte 0, 1, 2, 0    # 1: U R F
    .byte 3, 2, 1, 0    # 2: D F R
    .byte 3, 4, 2, 0    # 3: D L F
    .byte 0, 5, 1, 0    # 4: U B R
    .byte 3, 1, 5, 0    # 5: D R B
    .byte 3, 5, 4, 0    # 6: D B L
    .byte 0, 4, 5, 0    # 7: U L B
rs_slot:                        # 位置 p 三格的左上角 (x * 4, y * 4)，補兩個 0
    .byte 36, 12, 36, 28, 16, 28, 0, 0      # 0: U F L
    .byte 52, 12, 72, 28, 52, 28, 0, 0      # 1: U R F
    .byte 52, 56, 52, 40, 72, 40, 0, 0      # 2: D F R
    .byte 36, 56, 16, 40, 36, 40, 0, 0      # 3: D L F
    .byte 52, 0, 108, 28, 88, 28, 0, 0      # 4: U B R
    .byte 52, 68, 88, 40, 108, 40, 0, 0     # 5: D R B
    .byte 36, 68, 124, 40, 0, 40, 0, 0      # 6: D B L
    .byte 36, 0, 0, 28, 124, 28, 0, 0       # 7: U L B

    .bss
    .align 4
rs_rows:                        # LED 第 0-19 列的起始位址
    .zero 80
rs_state:                       # 位置 0-7 的 cubie @0-7、twist @8-15
    .zero 16
rs_next:                        # 轉一個四分之一圈時的暫存，格式同上
    .zero 16

    .text
# ---------------------------------------------------------------------------
# render_init：建立角塊狀態、算好每一列的位址、清空 LED，畫出初始狀態
# ---------------------------------------------------------------------------
render_init:
    # 角塊狀態：read_state 已經確認 input 是合法的狀態
    la      x13 input
    la      x14 rs_state
    sb      x0  0(x14)          # 位置 0：固定的 cubie 0
    sb      x0  8(x14)          #         twist 0
    addi    x15 x14 1           # 位置 1
    addi    x16 x14 8           # 位置 7 的下一個
rs_init_cubie:
    lbu     x18 0(x13)          # 排列數字 '1'-'7'
    addi    x18 x18 -48         # cubie 1-7
    sb      x18 0(x15)
    lbu     x18 7(x13)          # 同一個位置的方向數字 '1'-'3'
    addi    x18 x18 -49         # twist 0-2
    sb      x18 8(x15)
    addi    x13 x13 1
    addi    x15 x15 1
    bne     x15 x16 rs_init_cubie

    # rows[y] = BASE + y * WIDTH * 4，用累加算，不用乘法
    li      x13 LED_MATRIX_0_BASE
    li      x14 LED_MATRIX_0_WIDTH
    slli    x14 x14 2           # 一列幾個 byte
    la      x15 rs_rows
    addi    x16 x15 80          # 20 列
    add     x18 x13 x0
rs_init_rows:
    sw      x18 0(x15)
    add     x18 x18 x14
    addi    x15 x15 4
    bne     x15 x16 rs_init_rows

    # 整個 LED 清成黑色：HEIGHT 列，每列 WIDTH 個
    li      x15 LED_MATRIX_0_HEIGHT
    add     x18 x13 x0
rs_clear_row:
    add     x19 x18 x14         # 這一列的結尾
rs_clear:
    sw      x0  0(x18)
    addi    x18 x18 4
    bne     x18 x19 rs_clear
    addi    x15 x15 -1
    bne     x15 x0  rs_clear_row
    jal     x0  rs_draw         # 畫完由 rs_draw 直接回到呼叫的地方

# ---------------------------------------------------------------------------
# render_move：照 frame（x24）這一步轉 turn + 1 個四分之一圈，再畫一次
#   next[i] = state[src[i]]，twist 再加 tw[i]（mod 3）
# ---------------------------------------------------------------------------
render_move:
    lbu     x13 4(x24)          # face
    lbu     x14 5(x24)          # turn
    slli    x13 x13 3           # 每個面的表 8 bytes
    la      x15 rs_src
    add     x15 x15 x13
    la      x16 rs_tw
    add     x16 x16 x13
    la      x18 rs_state
    la      x19 rs_next
    addi    x10 x0  3
    addi    x11 x0  8
rs_quarter:
    addi    x20 x0  0           # 位置 i
rs_quarter_pos:
    add     x21 x15 x20
    lbu     x21 0(x21)          # src[i]
    add     x21 x18 x21
    lbu     x22 0(x21)          # 從那裡來的 cubie
    lbu     x23 8(x21)          # 和它的 twist
    add     x26 x16 x20
    lbu     x26 0(x26)          # tw[i]
    add     x23 x23 x26
    blt     x23 x10 rs_twist_ok
    addi    x23 x23 -3          # mod 3
rs_twist_ok:
    add     x27 x19 x20
    sb      x22 0(x27)
    sb      x23 8(x27)
    addi    x20 x20 1
    bne     x20 x11 rs_quarter_pos
    lw      x21 0(x19)          # next 複製回 state（16 bytes）
    sw      x21 0(x18)
    lw      x21 4(x19)
    sw      x21 4(x18)
    lw      x21 8(x19)
    sw      x21 8(x18)
    lw      x21 12(x19)
    sw      x21 12(x18)
    addi    x14 x14 -1
    bge     x14 x0  rs_quarter  # 一共轉 turn + 1 下
                                # 往下掉到 rs_draw

# ---------------------------------------------------------------------------
# rs_draw：把 rs_state 畫成展開圖，停 RENDER_DELAY，回到 x1
# ---------------------------------------------------------------------------
rs_draw:
    li      x13 LED_MATRIX_0_WIDTH
    slli    x13 x13 2           # 一列幾個 byte
    la      x14 rs_state
    la      x15 rs_slot
    la      x16 rs_colors
    la      x18 rs_palette
    la      x19 rs_rows
    addi    x10 x0  3
    addi    x11 x0  8
    addi    x20 x0  0           # 位置 p
rs_draw_pos:
    add     x21 x14 x20
    lbu     x22 0(x21)          # cubie
    lbu     x23 8(x21)          # twist t
    slli    x22 x22 2
    add     x22 x16 x22         # &rs_colors[cubie]
    slli    x21 x20 3
    add     x21 x15 x21         # &rs_slot[p]
    addi    x26 x0  0           # 第 k 格
rs_draw_slot:
    add     x27 x26 x23         # (k + t) mod 3
    blt     x27 x10 rs_color_ok
    addi    x27 x27 -3
rs_color_ok:
    add     x27 x22 x27
    lbu     x27 0(x27)          # 這格是哪一面的顏色
    slli    x27 x27 2
    add     x27 x18 x27
    lw      x27 0(x27)          # 0x00RRGGBB
    lbu     x28 0(x21)          # x * 4
    lbu     x29 1(x21)          # y * 4
    add     x29 x19 x29
    lw      x29 0(x29)          # rows[y]
    add     x29 x29 x28         # 這格左上角的位址
    sw      x27 0(x29)          # 4 × 3 像素
    sw      x27 4(x29)
    sw      x27 8(x29)
    sw      x27 12(x29)
    add     x29 x29 x13
    sw      x27 0(x29)
    sw      x27 4(x29)
    sw      x27 8(x29)
    sw      x27 12(x29)
    add     x29 x29 x13
    sw      x27 0(x29)
    sw      x27 4(x29)
    sw      x27 8(x29)
    sw      x27 12(x29)
    addi    x21 x21 2           # 下一格的座標
    addi    x26 x26 1
    bne     x26 x10 rs_draw_slot
    addi    x20 x20 1
    bne     x20 x11 rs_draw_pos

    li      x30 RENDER_DELAY    # 停一下，讓 GUI 看得到這一步
rs_wait:
    addi    x30 x30 -1
    bne     x30 x0  rs_wait
    jalr    x0  x1  0
