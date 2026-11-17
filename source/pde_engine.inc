	@; Phosphor_DE cheat engine (runs on every interrupt while cheats are on).
	@; Each entry is two words:  w0 = op<<28 | address,  w1 = value
	@; ops: 0 write8  1 write16  2 or16  3 and16  4 add16
	@;      5 if==  6 if!=  7 if>  8 if<  9 if&   16-bit compare with w1 low half;
	@;        when false, skip the next (w1>>16)&0xFF entries
	@;      10 if keys held: w1 low = key mask (all held -> run), skip as above
	@;      11 fill: write16 w1 at address, next entry = { repeat | value step<<16, address step }
	stmfd   SP!, {r1,r4-r7}
	adrl 		r5,CHEAT
	adrl		r6,Cheat_count
	ldr			r6,[r6]
cheat_loop:
	subs		r6,r6,#1
	bmi			cheat_loop_end
	ldmia   r5!,{r3-r4}
	mov			r7,r3,lsr#28
	bic			r3,r3,#0xF0000000
	cmp			r7,#11
	bhi			cheat_loop
	add			pc,pc,r7,lsl#2
	nop
	b				op_w8
	b				op_w16
	b				op_or
	b				op_and
	b				op_add
	b				op_cond
	b				op_cond
	b				op_cond
	b				op_cond
	b				op_cond
	b				op_keys
	b				op_fill
op_w8:
	strb		r4,[r3]
	b				cheat_loop
op_w16:
	strh		r4,[r3]
	b				cheat_loop
op_or:
	ldrh		r2,[r3]
	orr			r2,r2,r4
	strh		r2,[r3]
	b				cheat_loop
op_and:
	ldrh		r2,[r3]
	and			r2,r2,r4
	strh		r2,[r3]
	b				cheat_loop
op_add:
	ldrh		r2,[r3]
	add			r2,r2,r4
	strh		r2,[r3]
	b				cheat_loop
op_keys:
	add			r2,r0,#0x100
	ldrh		r2,[r2,#0x30]
	mov			r1,r4,lsl#16
	tst			r2,r1,lsr#16
	beq			cheat_loop
	b				cond_false
op_cond:
	ldrh		r2,[r3]
	mov			r1,r4,lsl#16
	mov			r1,r1,lsr#16
	cmp			r7,#5
	beq			c_eq
	cmp			r7,#6
	beq			c_ne
	cmp			r7,#7
	beq			c_gt
	cmp			r7,#8
	beq			c_lt
	tst			r2,r1
	bne			cheat_loop
	b				cond_false
c_eq:
	cmp			r2,r1
	beq			cheat_loop
	b				cond_false
c_ne:
	cmp			r2,r1
	bne			cheat_loop
	b				cond_false
c_gt:
	cmp			r2,r1
	bhi			cheat_loop
	b				cond_false
c_lt:
	cmp			r2,r1
	bcc			cheat_loop
cond_false:
	mov			r1,r4,lsr#16
	and			r1,r1,#0xFF
	cmp			r1,r6
	movhi		r1,r6
	sub			r6,r6,r1
	add			r5,r5,r1,lsl#3
	b				cheat_loop
op_fill:
	subs		r6,r6,#1
	bmi			cheat_loop_end
	ldmia   r5!,{r1,r7}
	mov			r2,r1,lsl#16
	movs		r2,r2,lsr#16
	beq			cheat_loop
fill_loop:
	strh		r4,[r3]
	add			r3,r3,r7
	add			r4,r4,r1,lsr#16
	subs		r2,r2,#1
	bne			fill_loop
	b				cheat_loop
cheat_loop_end:
	ldmfd   SP!, {r1,r4-r7}
