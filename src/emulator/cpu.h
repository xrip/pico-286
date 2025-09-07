/*
  PCulator: A portable, open-source x86 PC emulator.
  Copyright (C)2025 Mike Chambers

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef _CPU_H_
#define _CPU_H_

#include <stdint.h>

#define FUNC_INLINE
#define FUNC_FORCE_INLINE


union _bytewordregs_ {
	uint32_t longregs[8];
	uint16_t wordregs[16];
	uint8_t byteregs[16];
};

extern	union _bytewordregs_ regs;
extern	uint8_t	opcode, segoverride, reptype, hltstate, isaddr32, isoper32, isCS32, iopl, nt, tr, cpl, startcpl, protected, paging, usegdt, nowrite, currentseg;
extern	uint8_t sib, sib_scale, sib_index, sib_base;
extern	uint16_t segregs[6];
extern	uint32_t segcache[6];
extern	uint8_t segis32[6];
extern	uint32_t seglimit[6];
extern	uint8_t doexception, exceptionval;
extern	uint32_t exceptionerr, exceptionip;
extern	uint32_t savecs, saveip, ip, useseg, oldsp;
extern	uint8_t a20_enabled;
extern	uint32_t cr[8], dr[8];
extern	uint8_t	tempcf, oldcf, cf, pf, af, zf, sf, tf, ifl, df, of, rf, v86f, acf, idf, mode, reg, rm;
extern	uint16_t oper1, oper2, res16, disp16, temp16, dummy, stacksize, frametemp;
extern	uint32_t oper1_32, oper2_32, res32, disp32;
extern	uint8_t	oper1b, oper2b, res8, disp8, temp8, nestlev, addrbyte;
extern	uint32_t sib_val, temp1, temp2, temp3, temp4, temp5, temp32, tempaddr32, frametemp32, ea;
extern	uint32_t gdtr, gdtl;
extern	uint32_t idtr, idtl;
extern	uint32_t ldtr, ldtl;
extern	uint32_t trbase, trlimit;
extern	uint32_t shadow_esp;
extern	uint8_t trtype;
extern	uint8_t have387;
extern	uint8_t bypass_paging;
extern	uint16_t ldt_selector, tr_selector;
extern	int32_t	result;
extern	uint16_t trap_toggle;
extern	uint64_t totalexec, temp64, temp64_2, temp64_3;
extern	void (*int_callback[256])(uint8_t);
extern int myexceptionerr, mynowrite, myexceptionval, mydoexception;

#define regeax 0
#define regecx 1
#define regedx 2
#define regebx 3
#define regesp 4
#define regebp 5
#define regesi 6
#define regedi 7

#define regax 0
#define regcx 2
#define regdx 4
#define regbx 6
#define regsp 8
#define regbp 10
#define regsi 12
#define regdi 14

#define CPU_AX  regs.wordregs[regax]
#define CPU_CX  regs.wordregs[regcx]
#define CPU_DX  regs.wordregs[regdx]
#define CPU_BX  regs.wordregs[regbx]
#define CPU_SP  regs.wordregs[regsp]
#define CPU_BP  regs.wordregs[regbp]
#define CPU_SI  regs.wordregs[regsi]
#define CPU_DI  regs.wordregs[regdi]
#define CPU_IP  ip

#define reges 0
#define regcs 1
#define regss 2
#define regds 3
#define regfs 4
#define reggs 5

#define CPU_ES segregs[reges]
#define CPU_CS segregs[regcs]
#define CPU_SS segregs[regss]
#define CPU_DS segregs[regds]
#define CPU_FS segregs[regfs]
#define CPU_GS segregs[reggs]

#ifdef __BIG_ENDIAN__
#define regal 1
#define regah 0
#define regcl 5
#define regch 4
#define regdl 9
#define regdh 8
#define regbl 13
#define regbh 12
#else
#define regal 0
#define regah 1
#define regcl 4
#define regch 5
#define regdl 8
#define regdh 9
#define regbl 12
#define regbh 13
#endif

#define CPU_AL  regs.byteregs[regal]
#define CPU_AH  regs.byteregs[regah]
#define CPU_CL  regs.byteregs[regcl]
#define CPU_CH  regs.byteregs[regch]
#define CPU_DL  regs.byteregs[regdl]
#define CPU_DH  regs.byteregs[regdh]
#define CPU_BL  regs.byteregs[regbl]
#define CPU_BH  regs.byteregs[regbh]

#define MEMORY_RANGE		0x4000000
#define MEMORY_MASK			0x3FFFFFF

extern FUNC_INLINE uint8_t cpu_read(uint32_t addr);
uint16_t cpu_readw(uint32_t addr);
extern FUNC_INLINE void cpu_write(uint32_t addr32, uint8_t value);
extern FUNC_INLINE void cpu_writew(uint32_t addr32, uint16_t value);
extern FUNC_INLINE void cpu_writel(uint32_t addr32, uint32_t value);
void cpu_writew_linear(uint32_t addr32, uint16_t value);
void cpu_writel_linear(uint32_t addr32, uint32_t value);
extern FUNC_INLINE uint16_t cpu_readw(uint32_t addr32);
extern FUNC_INLINE uint32_t cpu_readl(uint32_t addr32);
uint16_t cpu_readw_linear(uint32_t addr32);
uint32_t cpu_readl_linear(uint32_t addr32);
void memory_mapRegister(uint32_t start, uint32_t len, uint8_t* readb, uint8_t* writeb);
void memory_mapCallbackRegister(uint32_t start, uint32_t count, uint8_t(*readb)(void*, uint32_t), void (*writeb)(void*, uint32_t, uint8_t), void* udata);
int memory_init();

void wrcache_init();
void wrcache_flush();

#define StepIP(x)	ip += x
#define getmem8(x, y)	cpu_read(x + y)
#define getmem16(x, y)	cpu_readw(x + y)
#define getmem32(x, y)	cpu_readl(x + y)
#define putmem8(x, y, z)	cpu_write(x + y, z)
#define putmem16(x, y, z)	cpu_writew(x + y, z)
#define putmem32(x, y, z)	cpu_writel(x + y, z)
#define signext(value)	(int16_t)(int8_t)(value)
#define signext8to32(value)	(int32_t)(int16_t)(int8_t)(value)
#define signext32(value)	(int32_t)(int16_t)(value)
#define signext64(value)	(int64_t)(int32_t)(int16_t)(value)
//#define getreg16(regid)	myregs.wordregs[regid]
#define getreg8(regid)	regs.byteregs[byteregtable[regid]]
//#define putreg16(regid, writeval)	myregs.wordregs[regid] = writeval
#define putreg8(regid, writeval)	regs.byteregs[byteregtable[regid]] = writeval
//#define getsegreg(regid)	mysegregs[regid]
//#define putsegreg(regid, writeval)	mysegregs[regid] = writeval
#define segbase(x)	(protected ? segtolinear(x) :((uint32_t) x << 4))
///#define exception(x, y) if (!mydoexception) { mydoexception = 1; myexceptionval = x; myexceptionerr = y; if (x == 14) mynowrite = 1; if (showops) debug_log(DEBUG_DETAIL, "EX: %u (%08X)\n", x, y); }
#define exception(x, y) if (!mydoexception) { mydoexception = 1; myexceptionval = x; myexceptionerr = y; if (x == 14) mynowrite = 1; }
#define ex_check(mycpu) if (mydoexception) break

#define EFLAGS_CF (1 << 0)
#define EFLAGS_PF (1 << 2)
#define EFLAGS_AF (1 << 4)
#define EFLAGS_ZF (1 << 6)
#define EFLAGS_SF (1 << 7)
#define EFLAGS_TF (1 << 8)
#define EFLAGS_IF (1 << 9)
#define EFLAGS_DF (1 << 10)
#define EFLAGS_OF (1 << 11)
#define EFLAGS_IOPL (3 << 12)
#define EFLAGS_NT (1 << 14)
#define EFLAGS_VM (1 << 17)

#define CR0_PE (1 << 0)
#define CR0_NE (1 << 5)

//Needs the following to detect as 8086/80186: (1 << 15) |
#define makeflagsword(x) \
	( \
	2 | (uint16_t) cf | ((uint16_t) pf << 2) | ((uint16_t) af << 4) | ((uint16_t) zf << 6) | ((uint16_t) sf << 7) | \
	((uint16_t) tf << 8) | ((uint16_t) ifl << 9) | ((uint16_t) df << 10) | ((uint16_t) of << 11) | \
	((uint32_t)iopl << 12) | ((uint32_t)nt << 14) | ((uint32_t)rf << 16) | ((uint32_t)v86f << 17) | ((uint32_t)acf << 18) | ((uint32_t) of << 11) | ((uint32_t) idf << 21) \
	)

#define decodeflagsword(y) { \
	uint32_t tmp; \
	tmp = y; \
	cf = tmp & 1; \
	pf = (tmp >> 2) & 1; \
	af = (tmp >> 4) & 1; \
	zf = (tmp >> 6) & 1; \
	sf = (tmp >> 7) & 1; \
	tf = (tmp >> 8) & 1; \
	if ((cpl == 0) || !protected) ifl = (tmp >> 9) & 1; \
	df = (tmp >> 10) & 1; \
	of = (tmp >> 11) & 1; \
	if ((cpl == 0) || !protected) iopl = (tmp >> 12) & 3; \
	nt = (tmp >> 14) & 1; \
	rf = (tmp >> 16) & 1; \
	v86f = (tmp >> 17) & 1; \
	if (v86f) cpl = 3; \
	acf = (tmp >> 18) & 1; \
	idf = (tmp >> 21) & 1; \
}

#define INT_SOURCE_EXCEPTION	0
#define INT_SOURCE_SOFTWARE		1
#define INT_SOURCE_HARDWARE		2

extern int showops;

FUNC_INLINE void cpu_intcall(uint8_t intnum, uint8_t source, uint32_t err);
void cpu_reset();
int cpu_interruptCheck(int slave);
void cpu_exec(uint32_t execloops);
void ports_init();
void port_write(uint16_t portnum, uint8_t value);
void port_writew(uint16_t portnum, uint16_t value);
void port_writel(uint16_t portnum, uint32_t value);
uint8_t port_read(uint16_t portnum);
uint16_t port_readw(uint16_t portnum);
uint32_t port_readl(uint16_t portnum);
void cpu_registerIntCallback(uint8_t interrupt, void (*cb)(uint8_t));
extern FUNC_INLINE uint16_t getreg16(uint8_t reg);
extern FUNC_INLINE void putreg16(uint8_t reg, uint16_t writeval);
extern FUNC_INLINE uint32_t getsegreg(uint8_t reg);
extern FUNC_INLINE void putsegreg(uint8_t reg, uint32_t writeval);
extern FUNC_FORCE_INLINE void modregrm();
extern FUNC_INLINE void getea(uint8_t rmval);

#define debug_log(...) ((void)0)

#endif
