# this should be second file
    .data
    .align 4
perm_row:
    .word perm_turn, perm_turn+10080, perm_turn+20160
ori_row:
    .word ori_turn, ori_turn+1458, ori_turn+2916
face_char:
    .string "RBD"
input:
    .string "21345671111111"


    .bss
frames:
    .zero 96 #12*8 stack 

