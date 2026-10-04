# Hand-written RV32I IDA*. Generate runnable source with rv32.py emit.
# No multiplication/division, heap, recursion, or compiler helper routines.
.equ RENDER, 0
.data
input: .string "21345671111111"
move_names: .string "R"
    .string "R2"
    .string "R'"
    .string "B"
    .string "B2"
    .string "B'"
    .string "D"
    .string "D2"
    .string "D'"
.align 2
# Six factoradic weights, each row padded to 16 bytes.
weights:
    .half 0,720,1440,2160,2880,3600,4320,0
    .half 0,120,240,360,480,600,0,0
    .half 0,24,48,72,96,0,0,0
    .half 0,6,12,18,0,0,0,0
    .half 0,2,4,0,0,0,0,0
    .half 0,1,0,0,0,0,0,0
.align 2
rows:
    .word 0,0
    .word 10080,1458
    .word 20160,2916
root: .word 0,0
length: .word 0
cube_p: .zero 7
cube_o: .zero 7
temporary: .zero 14
path: .zero 11
.align 2
# Frame: p,o,next_p,next_o,face,turn,previous_face (7 words).
frames: .zero 308
sources: .byte 1,4,2,0,3,5,6, 0,1,2,4,5,6,3, 0,2,5,3,1,4,6
twists: .byte 1,2,0,2,1,0,0, 0,0,0,1,2,1,2, 0,0,0,0,0,0,0
.text
.globl _start
_start:
    la t0,input
    la t1,cube_p
    li t2,0
    li t3,7
    li t6,7
parse_p:
    lbu t4,0(t0)
    addi t4,t4,-49
    bgeu t4,t6,invalid
    li t5,1
    sll t5,t5,t4
    and a0,t2,t5
    bnez a0,invalid
    or t2,t2,t5
    sb t4,0(t1)
    addi t0,t0,1
    addi t1,t1,1
    addi t3,t3,-1
    bnez t3,parse_p
    la t1,cube_o
    li t2,0
    li t3,7
    li t6,3
parse_o:
    lbu t4,0(t0)
    addi t4,t4,-49
    bgeu t4,t6,invalid
    add t2,t2,t4
    sb t4,0(t1)
    addi t0,t0,1
    addi t1,t1,1
    addi t3,t3,-1
    bnez t3,parse_o
    lbu t4,0(t0)
    bnez t4,invalid
reduce_sum:
    bltu t2,t6,sum_reduced
    addi t2,t2,-3
    j reduce_sum
sum_reduced:
    bnez t2,invalid
    li s3,0
    li s4,0
    la t0,cube_p
    la t1,cube_o
    la t2,weights
    la t3,cube_p
    addi t3,t3,7
rank_position:
    lbu t4,0(t0)
    addi t5,t0,1
    li t6,0
rank_smaller:
    lbu a0,0(t5)
    sltu a0,a0,t4
    add t6,t6,a0
    addi t5,t5,1
    bltu t5,t3,rank_smaller
    slli t6,t6,1
    add t6,t2,t6
    lhu t6,0(t6)
    add s3,s3,t6
    slli a0,s4,1
    add s4,s4,a0
    lbu a0,0(t1)
    add s4,s4,a0
    addi t0,t0,1
    addi t1,t1,1
    addi t2,t2,16
    addi a0,t0,1
    bltu a0,t3,rank_position
    la t0,root
    sw s3,0(t0)
    sw s4,4(t0)
    or t0,s3,s4
    beqz t0,search_done
    la a1,rows
    la a2,permutation
    la a3,orientation
    la a4,permutation_distance
    la a5,orientation_distance
    add t0,a4,s3
    lbu s2,0(t0)
    add t0,a5,s4
    lbu t0,0(t0)
    bgeu s2,t0,new_bound
    mv s2,t0
new_bound:
    li t0,11
    bgtu s2,t0,failed
    la t0,root
    lw s3,0(t0)
    lw s4,4(t0)
    la s0,frames
    li a6,0
    addi s1,s2,-1
    li s7,0
    li s9,255
next_face:
    bne s7,s9,face_allowed
    addi s7,s7,1
face_allowed:
    li t0,3
    beq s7,t0,backtrack
    slli t0,s7,3
    add t0,t0,a1
    lw s10,0(t0)
    add s10,s10,a2
    lw s11,4(t0)
    add s11,s11,a3
    mv s5,s3
    mv s6,s4
    li s8,0
next_turn:
    li t0,3
    beq s8,t0,advance_face
    slli t0,s5,1
    add t0,s10,t0
    lhu s5,0(t0)
    slli t0,s6,1
    add t0,s11,t0
    lhu s6,0(t0)
    addi s8,s8,1
    add t0,a4,s5
    lbu t0,0(t0)
    add t1,a5,s6
    lbu t1,0(t1)
    bgeu t0,t1,eager_max_ready
    mv t0,t1
eager_max_ready:
    bgtu t0,s1,next_turn
    slli t0,s7,1
    add t0,t0,s7
    add t0,t0,s8
    addi t0,t0,-1
    la t1,path
    add t1,t1,a6
    sb t0,0(t1)
    or t0,s5,s6
    beqz t0,found
    sw s3,0(s0)
    sw s4,4(s0)
    sw s5,8(s0)
    sw s6,12(s0)
    sw s7,16(s0)
    sw s8,20(s0)
    sw s9,24(s0)
    addi s0,s0,28
    addi a6,a6,1
    addi s1,s1,-1
    mv s3,s5
    mv s4,s6
    mv s9,s7
    li s7,0
    j next_face
advance_face:
    addi s7,s7,1
    j next_face
backtrack:
    beqz a6,raise_bound
    addi s0,s0,-28
    addi a6,a6,-1
    addi s1,s1,1
    lw s3,0(s0)
    lw s4,4(s0)
    lw s5,8(s0)
    lw s6,12(s0)
    lw s7,16(s0)
    lw s8,20(s0)
    lw s9,24(s0)
    slli t0,s7,3
    add t0,t0,a1
    lw s10,0(t0)
    add s10,s10,a2
    lw s11,4(t0)
    add s11,s11,a3
    j next_turn
raise_bound:
    addi s2,s2,1
    j new_bound
found:
    addi a6,a6,1
    la t0,length
    sw a6,0(t0)
search_done:
.if RENDER
    jal ra,draw_cube
.endif
    la s0,path
    la t0,length
    lw s1,0(t0)
replay_move:
    beqz s1,verify_cube
    lbu s2,0(s0)
    li t0,0
    li t1,3
decode_face:
    bltu s2,t1,face_decoded
    addi s2,s2,-3
    addi t0,t0,1
    j decode_face
face_decoded:
    addi s3,s2,1
    slli t1,t0,3
    sub t1,t1,t0
    la s4,sources
    add s4,s4,t1
    la s5,twists
    add s5,s5,t1
quarter_turn:
    li s6,0
    la t0,temporary
    la t1,cube_p
    la t2,cube_o
replay_cubie:
    add t3,s4,s6
    lbu t3,0(t3)
    add t4,t1,t3
    lbu t4,0(t4)
    sb t4,0(t0)
    add t3,t2,t3
    lbu t3,0(t3)
    add t4,s5,s6
    lbu t4,0(t4)
    add t3,t3,t4
    addi t3,t3,-3
    bltz t3,twist_small
    j twist_ready
twist_small:
    addi t3,t3,3
twist_ready:
    sb t3,7(t0)
    addi t0,t0,1
    addi s6,s6,1
    li t3,7
    bltu s6,t3,replay_cubie
    la t0,temporary
    la t1,cube_p
    li t2,14
copy_cube:
    lbu t3,0(t0)
    sb t3,0(t1)
    addi t0,t0,1
    addi t1,t1,1
    addi t2,t2,-1
    bnez t2,copy_cube
    addi s3,s3,-1
    bnez s3,quarter_turn
.if RENDER
    jal ra,draw_cube
.endif
    addi s0,s0,1
    addi s1,s1,-1
    j replay_move
verify_cube:
    la t0,cube_p
    li t1,0
    li t2,7
verify_cubie:
    lbu t3,0(t0)
    bne t3,t1,failed
    lbu t3,7(t0)
    bnez t3,failed
    addi t0,t0,1
    addi t1,t1,1
    bltu t1,t2,verify_cubie
    la s0,path
    la t0,length
    lw s1,0(t0)
    li s2,0
print_move:
    beq s2,s1,print_end
    beqz s2,print_name
    li a0,32
    li a7,11
    ecall
print_name:
    lbu t0,0(s0)
    la t1,move_names
    beqz t0,name_ready
skip_name:
    lbu t2,0(t1)
    addi t1,t1,1
    bnez t2,skip_name
    addi t0,t0,-1
    bnez t0,skip_name
name_ready:
    lbu a0,0(t1)
    beqz a0,name_end
    li a7,11
    ecall
    addi t1,t1,1
    j name_ready
name_end:
    addi s0,s0,1
    addi s2,s2,1
    j print_move
print_end:
    li a0,10
    li a7,11
    ecall
    li a0,0
    j exit
invalid:
    li a0,2
    j exit
failed:
    li a0,1
exit:
    li a7,93
    ecall
.if RENDER
.data
.align 2
palette: .word 0xffffff,0xff0000,0x00bb00,0xffff00,0xff8800,0x0000ff
# Home sticker colours; orientation adds to the local sticker slot.
stickers: .byte 0,1,2, 3,2,1, 3,4,2, 0,5,1, 3,1,5, 3,5,4, 0,4,5, 0,2,4
# corner, local sticker slot, x, y. The fixed front-upper-left corner is 7.
facelets:
    .byte 6,0,9,0, 3,0,13,0, 7,0,9,3, 0,0,13,3
    .byte 6,1,0,7, 7,2,4,7, 5,2,0,10, 2,1,4,10
    .byte 7,1,9,7, 0,2,13,7, 2,2,9,10, 1,1,13,10
    .byte 0,1,18,7, 3,2,22,7, 1,2,18,10, 4,1,22,10
    .byte 3,1,27,7, 6,2,31,7, 4,2,27,10, 5,1,31,10
    .byte 2,0,9,14, 1,0,13,14, 5,0,9,17, 4,0,13,17
.text
# Leaf renderer clobbers only t/a registers; replay state is in s registers.
draw_cube:
    li t0,LED_MATRIX_0_BASE
    li t1,LED_MATRIX_0_HEIGHT
clear_row:
    li t2,LED_MATRIX_0_WIDTH
clear_pixel:
    sw zero,0(t0)
    addi t0,t0,4
    addi t2,t2,-1
    bnez t2,clear_pixel
    addi t1,t1,-1
    bnez t1,clear_row
    la t0,facelets
    li a0,24
draw_facelet:
    lbu t1,0(t0)
    lbu t2,1(t0)
    li t3,7
    beq t1,t3,fixed_corner
    la t3,cube_o
    add t3,t3,t1
    lbu t3,0(t3)
    add t2,t2,t3
    li t3,3
    bltu t2,t3,slot_ready
    addi t2,t2,-3
slot_ready:
    la t3,cube_p
    add t3,t3,t1
    lbu t1,0(t3)
fixed_corner:
    slli t3,t1,1
    add t1,t1,t3
    add t1,t1,t2
    la t3,stickers
    add t1,t1,t3
    lbu t1,0(t1)
    slli t1,t1,2
    la t3,palette
    add t1,t1,t3
    lw a3,0(t1)
    li a1,LED_MATRIX_0_WIDTH
    slli a1,a1,2
    li t1,LED_MATRIX_0_BASE
    lbu t2,3(t0)
    beqz t2,y_ready
pixel_y:
    add t1,t1,a1
    addi t2,t2,-1
    bnez t2,pixel_y
y_ready:
    lbu t2,2(t0)
    slli t2,t2,2
    add t1,t1,t2
    li a2,3
sticker_row:
    sw a3,0(t1)
    sw a3,4(t1)
    sw a3,8(t1)
    sw a3,12(t1)
    add t1,t1,a1
    addi a2,a2,-1
    bnez a2,sticker_row
    addi t0,t0,4
    addi a0,a0,-1
    bnez a0,draw_facelet
    li t0,3000000
animation_pause:
    addi t0,t0,-1
    bnez t0,animation_pause
    ret
.endif
