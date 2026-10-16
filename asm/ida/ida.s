# this should the thrid file
# constants
    .equ MAX_DEPTH, 11
    .equ NO_FACE, 3

# 全程暫存器（不照 C ABI，x1-x4 也拿來用）
#   x3  = frames 位址（搜尋 stack，frame g 在 x3 + (g << 3)）
#   x4  = perm_row 位址
#   x5  = ori_row 位址
#   x6  = perm_dist 位址
#   x7  = ori_dist 位址
#   x8  = p（read_state 算出，check_path 還要用）
#   x9  = o（同上）
#   x10 = ecall 參數／狀態碼（a0）
#   x11 = bound（solve 設定，search 只讀）
#   x12 = length（search 的結果，0 表示這個 bound 沒有解）
#   x17 = ecall 編號（a7）
#   其他 x1 x2 x13-x16 x18-x31：暫存，每一段自由使用

# main code
    .text
main:
# ---------------------------------------------------------------------------
# 0. 準備：整個程式都會用到的位址，放進固定暫存器
# ---------------------------------------------------------------------------
    la      x3  frames
    la      x4  perm_row
    la      x5  ori_row
    la      x6  perm_dist
    la      x7  ori_dist

# ---------------------------------------------------------------------------
# 1. read_state (ida.c:47)
#   輸入：input 字串（ida_data.s）
#   輸出：x8 = p、x9 = o；不合法就跳 bad_input
#   只跑一次，所以直接往下掉，不用 jal/ret。迴圈全部展開。
#   排列合法就先算 p，再檢查方向；不合法的輸入多做幾條指令，結果和 C 相同。
# ---------------------------------------------------------------------------
read_state:
    # d0-d6 = x18-x24，c1-c5 = x25-x29，seen = x1，暫存 x30 x31
    # ---- 1a. 排列 s[0..6] → p ----
    la      x11 input
    lbu     x18 0(x11)          # 先讀完再用，pipeline 上沒有 load-use stall
    lbu     x19 1(x11)
    lbu     x20 2(x11)
    lbu     x21 3(x11)
    lbu     x22 4(x11)
    lbu     x23 5(x11)
    lbu     x24 6(x11)
    addi    x18 x18 -49         # d0 = s[0] - '1'
    addi    x19 x19 -49         # d1
    addi    x20 x20 -49         # d2
    addi    x21 x21 -49         # d3
    addi    x22 x22 -49         # d4
    addi    x23 x23 -49         # d5
    addi    x24 x24 -49         # d6

    # 範圍：d > 6 不合法。unsigned 比較，'\0' 減 '1' 會變成很大的數
    addi    x12 x0  7
    bgeu    x18 x12 bad_input
    bgeu    x19 x12 bad_input
    bgeu    x20 x12 bad_input
    bgeu    x21 x12 bad_input
    bgeu    x22 x12 bad_input
    bgeu    x23 x12 bad_input
    bgeu    x24 x12 bad_input

    # 重複：seen = OR(1 << d)。7 個 d 都在 0-6，所以 7 個 bit 全亮
    # ⇔ 沒有重複（鴿籠原理），不用每一位都檢查。
    # 上面的範圍檢查不能省：sll 只看低 5 bits，'Q' - '1' = 32 會撞到 bit 0
    addi    x13 x0  1
    sll     x1  x13 x18
    sll     x30 x13 x19
    or      x1  x1  x30
    sll     x30 x13 x20
    or      x1  x1  x30
    sll     x30 x13 x21
    or      x1  x1  x30
    sll     x30 x13 x22
    or      x1  x1  x30
    sll     x30 x13 x23
    or      x1  x1  x30
    sll     x30 x13 x24
    or      x1  x1  x30
    addi    x30 x0  127
    bne     x1  x30 bad_input

    # c_i = d_i - (前面比 d_i 小的個數)
    # d 是 0-6 的排列，比 d_i 小的數剛好 d_i 個，扣掉在前面的，剩下在後面的
    # 就是 ida.c 的 c[i]。這樣只要 15 次比較（C 是 21 次）。c0 就是 d0。
    sltu    x30 x18 x19         # d0 < d1
    sub     x25 x19 x30         # c1
    sltu    x30 x18 x20         # d0 < d2
    sub     x26 x20 x30
    sltu    x30 x19 x20         # d1 < d2
    sub     x26 x26 x30         # c2
    sltu    x30 x18 x21         # d0 < d3
    sub     x27 x21 x30
    sltu    x30 x19 x21         # d1 < d3
    sub     x27 x27 x30
    sltu    x30 x20 x21         # d2 < d3
    sub     x27 x27 x30         # c3
    sltu    x30 x18 x22         # d0 < d4
    sub     x28 x22 x30
    sltu    x30 x19 x22         # d1 < d4
    sub     x28 x28 x30
    sltu    x30 x20 x22         # d2 < d4
    sub     x28 x28 x30
    sltu    x30 x21 x22         # d3 < d4
    sub     x28 x28 x30         # c4
    sltu    x30 x18 x23         # d0 < d5
    sub     x29 x23 x30
    sltu    x30 x19 x23         # d1 < d5
    sub     x29 x29 x30
    sltu    x30 x20 x23         # d2 < d5
    sub     x29 x29 x30
    sltu    x30 x21 x23         # d3 < d5
    sub     x29 x29 x30
    sltu    x30 x22 x23         # d4 < d5
    sub     x29 x29 x30         # c5

    # p = ((((c0 * 6 + c1) * 5 + c2) * 4 + c3) * 3 + c4) * 2 + c5
    slli    x30 x18 2
    slli    x31 x18 1
    add     x8  x30 x31         # c0 * 6
    add     x8  x8  x25         # + c1
    slli    x30 x8  2
    add     x8  x8  x30         # p * 5
    add     x8  x8  x26         # + c2
    slli    x8  x8  2           # p * 4
    add     x8  x8  x27         # + c3
    slli    x30 x8  1
    add     x8  x8  x30         # p * 3
    add     x8  x8  x28         # + c4
    slli    x8  x8  1           # p * 2
    add     x8  x8  x29         # + c5

    # ---- 1b. 方向 s[7..13] → o，s[14] 必須是 '\0' ----
    # e0-e6 = x18-x24，s[14] = x25，sum = x2，暫存 x30
    lbu     x18 7(x11)
    lbu     x19 8(x11)
    lbu     x20 9(x11)
    lbu     x21 10(x11)
    lbu     x22 11(x11)
    lbu     x23 12(x11)
    lbu     x24 13(x11)
    lbu     x25 14(x11)
    addi    x18 x18 -49         # e0 = s[7] - '1'
    addi    x19 x19 -49         # e1
    addi    x20 x20 -49         # e2
    addi    x21 x21 -49         # e3
    addi    x22 x22 -49         # e4
    addi    x23 x23 -49         # e5
    addi    x24 x24 -49         # e6

    # 範圍：e > 2 不合法；長度必須剛好 14
    addi    x12 x0  3
    bgeu    x18 x12 bad_input
    bgeu    x19 x12 bad_input
    bgeu    x20 x12 bad_input
    bgeu    x21 x12 bad_input
    bgeu    x22 x12 bad_input
    bgeu    x23 x12 bad_input
    bgeu    x24 x12 bad_input
    bne     x25 x0  bad_input

    # sum 必須是 3 的倍數。e 都在 0-2，所以 sum 最多 14：
    # 查 0x1249 的第 sum 個 bit（bit 0 3 6 9 12 是 1），不用 mod
    add     x2  x18 x19
    add     x2  x2  x20
    add     x2  x2  x21
    add     x2  x2  x22
    add     x2  x2  x23
    add     x2  x2  x24
    li      x30 0x1249
    srl     x30 x30 x2
    andi    x30 x30 1
    beq     x30 x0  bad_input

    # o：e0-e5 的 3 進位，e6 由其他六個決定
    slli    x30 x18 1
    add     x9  x30 x18         # e0 * 3
    add     x9  x9  x19         # + e1
    slli    x30 x9  1
    add     x9  x9  x30         # o * 3
    add     x9  x9  x20         # + e2
    slli    x30 x9  1
    add     x9  x9  x30         # o * 3
    add     x9  x9  x21         # + e3
    slli    x30 x9  1
    add     x9  x9  x30         # o * 3
    add     x9  x9  x22         # + e4
    slli    x30 x9  1
    add     x9  x9  x30         # o * 3
    add     x9  x9  x23         # + e5


    #clear reg
    addi    x12 x0  0
# ---------------------------------------------------------------------------
# 2. solve (ida.c:161)                                               TODO
#   輸入：x8 = p、x9 = o
#   - bound = max(perm_dist[p], ori_dist[o])，用 x6、x7 查（lbu）
#   - bound = x11
#   - bound == 0：輸入已經是 solved，length = 0，直接去 print_path
#   - frames[0]：p = x8、o = x9、skip = NO_FACE
#     frame 8 bytes：p @0、o @2（sh），face @4、turn @5、skip @6（sb）
#   - bound 從上面的值加到 MAX_DEPTH，每輪跑一次 search，找到就去 print_path
#   - 超過 MAX_DEPTH 還沒找到：exit_fail
# ---------------------------------------------------------------------------
solve:
    add     x13 x6  x8          # &perm_dist[p]（x6、x7 不能改，search 要用）
    add     x14 x7  x9          # &ori_dist[o]
    lbu     x21 0(x13)          # x21 = perm_dist[p]
    lbu     x22 0(x14)          # x22 = ori_dist[o]
    slt     x18 x21  x22
    addi    x19 x0 1
    beq     x18  x19 seven_bigger
    slt     x18 x22  x21
    beq     x18 x19 six_bigger
    bne     x21  x0  six_seven
    jal     x0  print_path # need to work out
seven_bigger:
    add     x11 x0  x22
    jal     x0  have_bigger_num
six_seven:
six_bigger:
    add     x11 x0  x21
have_bigger_num:
    sh      x8  0(x3)
    sh      x9  2(x3)
    addi    x19 x0  NO_FACE
    sb      x19 6(x3)
try_longer_depth:
    addi    x20 x0  MAX_DEPTH
    bltu    x20 x11 exit_fail
    jal     x0  search
search_return:
    bne     x12 x0  print_path
    addi    x11 x11 1
    jal     x0 try_longer_depth
    


# ---------------------------------------------------------------------------
# 3. search(bound) (ida.c:132)，含 start_face (ida.c:92)、next_child (ida.c:105)
#   輸入：x11 = bound（>= 1）；frames[0] 的 p、o、skip 已經由 solve 設好
#   輸出：x12 = length，0 表示這個 bound 沒有解；結束一律跳 search_return
#   子節點的順序和 ida.c 相同，所以找到的解也相同。
#
#   frame g 在 x3 + (g << 3)：p @0、o @2、face @4、turn @5、skip @6。
#   正在試的子節點就是 frame g + 1 的 p、o。這一層的 face、turn、子節點都
#   放在暫存器，只在往下一層時寫回 frame，退回時再讀出來。
#     x13 = F，目前 frame 的位址（取代 g）
#     x14 = limit = bound - g - 1（往下 -1、退回 +1，不用每次重算）
#     x15 = face    x16 = turn    x18 = skip
#     x19 = perm_row[face]    x20 = ori_row[face]
#     x21 = 子節點的 p    x22 = 子節點的 o
#     x23 = 常數 2    x28 x29 = 暫存
#   最常走的路是 next_child → check → 剪掉 → next_child，
#   所以這三段排在一起，直接往下掉。
# ---------------------------------------------------------------------------
search:
    add     x13 x3  x0          # F = &frames[0]
    addi    x14 x11 -1          # limit = bound - 1
    addi    x15 x0  0           # 根節點從 R 開始
    addi    x18 x0  NO_FACE     # 根節點沒有要跳過的面
    addi    x23 x0  2
start_face:                     # 這一層開始試 face：child 先設成節點本身、
                                # turn = -1，往下掉到 next_child 轉第一下
    slli    x28 x15 2
    add     x29 x4  x28
    lw      x19 0(x29)          # perm_row[face]
    add     x29 x5  x28
    lw      x20 0(x29)          # ori_row[face]
    lhu     x21 0(x13)          # child.p = frame.p
    lhu     x22 2(x13)          # child.o = frame.o
    addi    x16 x0  -1
next_child:                     # 同一面再轉一下：R → R2 → R'
    beq     x16 x23 next_face   # 三下都試過了，換下一面
    addi    x16 x16 1
    slli    x28 x21 1
    add     x28 x19 x28
    lhu     x21 0(x28)          # child.p = perm_row[face][child.p]
    slli    x28 x22 1
    add     x28 x20 x28
    lhu     x22 0(x28)          # child.o = ori_row[face][child.o]
check:                          # 任何一張表說還要超過 limit 步就剪掉
    add     x28 x6  x21
    lbu     x28 0(x28)          # perm_dist[child.p]
    bltu    x14 x28 next_child  # 先查 perm，超過就不用讀 ori
    add     x28 x7  x22
    lbu     x28 0(x28)          # ori_dist[child.o]
    bltu    x14 x28 next_child
    # 只有 solved 在兩張表都是 0，所以 limit 0 還通過剪枝就是 solved
    beq     x14 x0  found
descend:                        # 往下一層：child 變成 frame g + 1 的節點
    sb      x15 4(x13)          # frame.face
    sb      x16 5(x13)          # frame.turn
    sh      x21 8(x13)          # frame[g + 1].p
    sh      x22 10(x13)         # frame[g + 1].o
    sb      x15 14(x13)         # frame[g + 1].skip = face
    addi    x13 x13 8           # g++
    addi    x14 x14 -1          # limit--
    add     x18 x15 x0          # skip = face
    sltiu   x15 x18 1           # face = (skip == 0) ? 1 : 0
    jal     x0  start_face
next_face:                      # 換下一面，跳過 skip
    addi    x15 x15 1
    bne     x15 x18 face_ok
    addi    x15 x15 1
face_ok:
    bgeu    x23 x15 start_face  # face <= 2：還有面可以試
backtrack:                      # 這一層都試完了，退回上一層
    beq     x13 x3  not_found   # 已經在根節點：這個 bound 沒有解
    addi    x13 x13 -8          # g--
    addi    x14 x14 1           # limit++
    lbu     x15 4(x13)          # 讀回上一層的 face、turn、skip
    lbu     x16 5(x13)
    lbu     x18 6(x13)
    lhu     x21 8(x13)          # 上一層正在試的 child，就是剛退出的那個節點
    lhu     x22 10(x13)
    slli    x28 x15 2
    add     x29 x4  x28
    lw      x19 0(x29)          # perm_row[face]
    add     x29 x5  x28
    lw      x20 0(x29)          # ori_row[face]
    jal     x0  next_child
found:
    sb      x15 4(x13)          # 最後一步的 face、turn 還在暫存器，寫回 frame
    sb      x16 5(x13)
    sub     x12 x13 x3
    srli    x12 x12 3
    addi    x12 x12 1           # length = g + 1
    jal     x0  search_return
not_found:
    addi    x12 x0  0
    jal     x0  search_return

# ---------------------------------------------------------------------------
# 4. print_path(length) (ida.c:196)
#   輸入：x12 = length；frames[0..length-1] 的 face、turn
#   格式和 solver.c 相同：R、R2、R'，中間一個空格，最後換行；length 0 只印換行
#   印一個字元：x17 = 11（PrintChar）、x10 = 字元、ecall
#   x24 = 目前的 frame、x25 = &frames[length]、x26 = face_char、x27 = 暫存
#   x8、x9、x12 不能動，check_path 還要用
# ---------------------------------------------------------------------------
print_path:
    addi    x17 x0  11
    add     x24 x3  x0
    slli    x25 x12 3
    add     x25 x3  x25
    la      x26 face_char
    beq     x24 x25 print_end   # length 0
print_move:
    lbu     x27 4(x24)          # face
    add     x27 x26 x27
    lbu     x10 0(x27)          # 'R'、'B'、'D'
    ecall
    lbu     x27 5(x24)          # turn
    beq     x27 x0  print_next  # turn 0：R，不加字
    addi    x10 x0  50          # '2'
    addi    x27 x27 -1
    beq     x27 x0  print_turn  # turn 1：R2
    addi    x10 x0  39          # turn 2：R'（39 是 '\''）
print_turn:
    ecall
print_next:
    addi    x24 x24 8
    beq     x24 x25 print_end
    addi    x10 x0  32          # ' '
    ecall
    jal     x0  print_move
print_end:
    addi    x10 x0  10          # '\n'
    ecall

# ---------------------------------------------------------------------------
# 5. check_path(p, o, length) (ida.c:182)，T5
#   從輸入的 (p, o) 出發，frames[0..length-1] 每一步轉 turn + 1 個四分之一圈，
#   用的是搜尋時同一組 perm_row、ori_row。最後必須是 (0, 0)，否則 exit_fail。
#   x24 = 目前的 frame、x25 = &frames[length]、x26 x27 = 這一步的兩個列、
#   x28 = 還要轉幾下、x29 = 暫存。x8、x9 直接拿來算。
# ---------------------------------------------------------------------------
check_path:
    add     x24 x3  x0
    slli    x25 x12 3
    add     x25 x3  x25
    beq     x24 x25 check_end   # length 0
check_move:
    lbu     x29 4(x24)          # face
    slli    x29 x29 2
    add     x26 x4  x29
    lw      x26 0(x26)          # perm_row[face]
    add     x27 x5  x29
    lw      x27 0(x27)          # ori_row[face]
    lbu     x28 5(x24)          # turn：要轉 turn + 1 下
check_turn:
    slli    x29 x8  1
    add     x29 x26 x29
    lhu     x8  0(x29)          # p = perm_row[face][p]
    slli    x29 x9  1
    add     x29 x27 x29
    lhu     x9  0(x29)          # o = ori_row[face][o]
    addi    x28 x28 -1
    bge     x28 x0  check_turn
    addi    x24 x24 8
    bne     x24 x25 check_move
check_end:
    or      x29 x8  x9
    bne     x29 x0  exit_fail   # 沒有回到 solved

# ---------------------------------------------------------------------------
# 6. 結束，狀態碼和 ida.c 的 main 相同。x17 = 93 是 Ripes 的 exit with code。
#   RV32_ISS 在 exit 的 ecall 之後還會多執行一條指令（也算進 --iret），
#   所以每個出口都先設 x17、再設 x10：多跑的那條是下一個出口的
#   addi x17 x0 93，不會動到任何暫存器的值。
# ---------------------------------------------------------------------------
exit_ok:
    addi    x17 x0  93
    addi    x10 x0  0
    ecall
exit_fail:                      # 找不到解，或 T5 失敗
    addi    x17 x0  93
    addi    x10 x0  1
    ecall
bad_input:                      # 輸入不是合法的狀態
    addi    x17 x0  93
    addi    x10 x0  2
    ecall
