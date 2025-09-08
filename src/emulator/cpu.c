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

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "cpu.h"
#include "fpu.h"
#include "emulator.h"

#if PICO_ON_DEVICE
	#include "disks-rp2350.c.inl"
	#include "network-redirector-rp2350.c.inl"
	#include "graphics.h"
#else
#include <stdio.h>
#include <stdbool.h>
	#include "disks-win32.c.inl"
	#include "network-redirector.c.inl"
#endif

#define MEMORY_RANGE XMS_MEMORY_SIZE

int videomode = 3;

// TODO:
void wrcache_init() {}
void wrcache_flush() {}

void port_writew(uint16_t portnum, uint16_t value) {
	port_write(portnum, (uint8_t)value);
	port_write(portnum + 1, (uint8_t)(value >> 8));
}

void port_writel(uint16_t portnum, uint32_t value) {
	port_write(portnum, (uint8_t)value);
	port_write(portnum + 1, (uint8_t)(value >> 8));
	port_write(portnum + 2, (uint8_t)(value >> 16));
	port_write(portnum + 3, (uint8_t)(value >> 24));
}

uint16_t port_readw(uint16_t portnum) {
	uint16_t ret = port_read(portnum);
	ret |= (uint16_t)port_read(portnum + 1) << 8;
	return ret;
}

uint32_t port_readl(uint16_t portnum) {
	uint32_t ret = port_read(portnum);
	ret |= (uint32_t)port_read(portnum + 1) << 8;
	ret |= (uint32_t)port_read(portnum + 2) << 16;
	ret |= (uint32_t)port_read(portnum + 3) << 24;
	return ret;
}
#ifdef debug_log
#undef debug_log
#endif
#define debug_log(X, ...) printf(__VA_ARGS__)

	union _bytewordregs_ regs;
	uint8_t	opcode, segoverride, reptype, hltstate, isaddr32, isoper32, isCS32, iopl, nt, tr, cpl, startcpl, protected, paging, usegdt, nowrite, currentseg;
	uint8_t sib, sib_scale, sib_index, sib_base;
	uint16_t segregs[6];
	uint32_t segcache[6];
	uint8_t segis32[6];
	uint32_t seglimit[6];
	uint8_t doexception, exceptionval;
	uint32_t exceptionerr, exceptionip;
	uint32_t savecs, saveip, ip, useseg, oldsp;
	uint8_t a20_gate = 0;
	uint32_t cr[8], dr[8];
	uint8_t	tempcf, oldcf, cf, pf, af, zf, sf, tf, ifl, df, of, rf, v86f, acf, idf, mode, reg, rm;
	uint16_t oper1, oper2, res16, disp16, temp16, dummy, stacksize, frametemp;
	uint32_t oper1_32, oper2_32, res32, disp32;
	uint8_t	oper1b, oper2b, res8, disp8, temp8, nestlev, addrbyte;
	uint32_t sib_val, temp1, temp2, temp3, temp4, temp5, temp32, tempaddr32, frametemp32, ea;
	uint32_t gdtr, gdtl;
	uint32_t idtr, idtl;
	uint32_t ldtr, ldtl;
	uint32_t trbase, trlimit;
	uint32_t shadow_esp;
	uint8_t trtype;
	uint8_t have387;
	uint8_t bypass_paging;
	uint16_t ldt_selector, tr_selector;
	int32_t	result;
	uint16_t trap_toggle;
	uint64_t totalexec, temp64, temp64_2, temp64_3;
	void (*int_callback[256])(uint8_t);

const uint8_t byteregtable[8] = { regal, regcl, regdl, regbl, regah, regch, regdh, regbh };

const uint8_t parity[0x100] = {
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

FUNC_INLINE void cpu_callf(uint16_t selector, uint32_t ip);
FUNC_INLINE void cpu_retf(uint32_t adjust);

//translate protected mode segments/selectors into linear addresses from descriptor table
uint32_t segtolinear(uint16_t seg) {
	uint32_t addr, gdtidx;

	if (!protected || v86f) return (uint32_t)seg << 4;

	gdtidx = ((seg & 4) ? ldtr : gdtr) + ((uint32_t)seg & ~7);
	addr = cpu_read(gdtidx + 2) | ((uint32_t)cpu_read(gdtidx + 3) << 8);  // Base Low (16-bit)
	addr |= ((uint32_t)cpu_read(gdtidx + 4) << 16);  // Base Mid (8-bit)
	addr |= ((uint32_t)cpu_read(gdtidx + 7) << 24);  // Base High (8-bit)
	//if (isoper32) printf("Entered 32-bit segment\n");
	//printf("segtolinear %04X = %08X\n", seg, addr);
	return addr;
}

void task_switch(uint16_t new_tss_selector, int is_task_gate) {
	printf("task_switch();\n");
	uint32_t new_desc_addr = gdtr + (new_tss_selector & ~7);
	uint8_t access = cpu_read(new_desc_addr + 5);
	uint8_t type = access & 0x0F;
	uint8_t present = access >> 7;

	if (!present || (type != 0x9 && type != 0xB)) {
///		exception(10, new_tss_selector); // Invalid TSS
		return;
	}

	// Get base and limit of new TSS
	uint32_t base = cpu_read(new_desc_addr + 2) |
		(cpu_read(new_desc_addr + 3) << 8) |
		(cpu_read(new_desc_addr + 4) << 16) |
		(cpu_read(new_desc_addr + 7) << 24);
	uint32_t limit = cpu_read(new_desc_addr) |
		(cpu_read(new_desc_addr + 1) << 8) |
		((cpu_read(new_desc_addr + 6) & 0x0F) << 16);

	// Save current task into its TSS (only if this is a task gate or CALL/JMP, not IRET)
	uint32_t old_tss_base = trbase;
	cpu_writel(old_tss_base + 0x20, ip);
	cpu_writel(old_tss_base + 0x24, makeflagsword());
	cpu_writel(old_tss_base + 0x28, regs.longregs[regeax]);
	cpu_writel(old_tss_base + 0x2C, regs.longregs[regecx]);
	cpu_writel(old_tss_base + 0x30, regs.longregs[regedx]);
	cpu_writel(old_tss_base + 0x34, regs.longregs[regebx]);
	cpu_writel(old_tss_base + 0x38, regs.longregs[regesp]);
	cpu_writel(old_tss_base + 0x3C, regs.longregs[regebp]);
	cpu_writel(old_tss_base + 0x40, regs.longregs[regesi]);
	cpu_writel(old_tss_base + 0x44, regs.longregs[regedi]);
	cpu_writew(old_tss_base + 0x00, segregs[reges]);
	cpu_writew(old_tss_base + 0x02, segregs[regcs]);
	cpu_writew(old_tss_base + 0x04, segregs[regss]);
	cpu_writew(old_tss_base + 0x06, segregs[regds]);
	cpu_writew(old_tss_base + 0x08, segregs[regfs]);
	cpu_writew(old_tss_base + 0x0A, segregs[reggs]);
	cpu_writew(old_tss_base + 0x5C, ldtr); // LDT
	cpu_writel(old_tss_base + 0x1C, cr[3]);

	// Mark old TSS as busy (set type bit 1)
	uint8_t old_access = cpu_read(gdtr + (tr_selector & ~7) + 5);
	if ((old_access & 0x0F) == 0x9) {
		cpu_write(gdtr + (tr_selector & ~7) + 5, old_access | 0x2);
	}

	if (!is_task_gate && type == 0x9) {
		//Set busy on new TSS
		cpu_write(new_desc_addr + 5, access | 0x2);
	}

	// Load new task
	ip = cpu_readl(base + 0x20);
	uint32_t new_eflags = cpu_readl(base + 0x24);
	regs.longregs[regeax] = cpu_readl(base + 0x28);
	regs.longregs[regecx] = cpu_readl(base + 0x2C);
	regs.longregs[regedx] = cpu_readl(base + 0x30);
	regs.longregs[regebx] = cpu_readl(base + 0x34);
	regs.longregs[regesp] = cpu_readl(base + 0x38);
	regs.longregs[regebp] = cpu_readl(base + 0x3C);
	regs.longregs[regesi] = cpu_readl(base + 0x40);
	regs.longregs[regedi] = cpu_readl(base + 0x44);
	putsegreg(reges, cpu_readw(base + 0x00));
	putsegreg(regcs, cpu_readw(base + 0x02));
	putsegreg(regss, cpu_readw(base + 0x04));
	putsegreg(regds, cpu_readw(base + 0x06));
	putsegreg(regfs, cpu_readw(base + 0x08));
	putsegreg(reggs, cpu_readw(base + 0x0A));
	ldtr = cpu_readw(base + 0x5C);

	// Paging? Load CR3
	if (cr[0] & 0x80000000) {
		cr[3] = cpu_readl(base + 0x1C);
	}

	// Save TSS selector and base
	tr_selector = new_tss_selector;
	trbase = base;

	// Update EFLAGS
	decodeflagsword(new_eflags);

	// Set NT flag
	if (is_task_gate) {
		nt = 1;
	}
}

FUNC_FORCE_INLINE void modregrm() {
	addrbyte = getmem8(segcache[regcs], ip);
	StepIP(1);
	mode = addrbyte >> 6;
	reg = (addrbyte >> 3) & 7;
	rm = addrbyte & 7;

	if (!isaddr32) {
		switch (mode) {
		case 0:
			if (rm == 6) {
				disp16 = getmem16(segcache[regcs], ip);
				StepIP(2);
			}
			if (((rm == 2) || (rm == 3)) && !segoverride) {
				useseg = segcache[regss];
			}
			break;

		case 1:
			disp16 = signext(getmem8(segcache[regcs], ip));
			StepIP(1);
			if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
				useseg = segcache[regss];
			}
			break;

		case 2:
			disp16 = getmem16(segcache[regcs], ip);
			StepIP(2);
			if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
				useseg = segcache[regss];
			}
			break;

		default:
			disp8 = 0;
			disp16 = 0;
		}
	}
	else { //32-bit addressing
		//printf("modr/m 32-bit\n");
		sib_val = 0;
		if ((mode < 3) && (rm == 4)) { // SIB byte present
			sib = getmem8(segcache[regcs], ip);
			StepIP(1);
			sib_scale = (sib >> 6) & 3;
			sib_index = (sib >> 3) & 7;
			sib_base = sib & 7;
		}

		if (!segoverride && mode < 3) {
			if (rm == 4) { // SIB
				if (sib_base == regesp || (sib_base == regebp && mode > 0)) {
					useseg = segcache[regss];
				}
			}
			else if (rm == 5 && mode > 0) {
				useseg = segcache[regss];
			}
		}


		switch (mode) {
		case 0:
			if ((rm == 5) || ((rm == 4) && (sib_base == 5))) {
				disp32 = getmem32(segcache[regcs], ip);
				StepIP(4);
			}
			else {
				disp32 = 0;
			}
			break;

		case 1:
			disp32 = signext8to32(getmem8(segcache[regcs], ip));
			StepIP(1);
			break;

		case 2:
			disp32 = getmem32(segcache[regcs], ip);
			StepIP(4);
			break;

		default:
			disp32 = 0;
		}

		if ((mode < 3) && (rm == 4)) { // SIB byte present
			uint32_t index, base;
			if (sib_index == regesp) { //ESP index actually means NO index
				index = 0;
			}
			else {
				index = regs.longregs[sib_index] << (uint32_t)sib_scale;
			}
			if (mode == 0 && sib_base == regebp) { //if base is EBP and mode is 0, there is actually NO base register
				base = 0;
			}
			else {
				base = (sib_base == regesp) ? shadow_esp : regs.longregs[sib_base];
				if (!segoverride && sib_base == 4) useseg = segcache[regss];
			}
			sib_val = base + index;
		}

	}
}

FUNC_INLINE uint16_t getreg16(uint8_t reg) {
	switch (reg) {
	case 0: return regs.wordregs[regax];
	case 1: return regs.wordregs[regcx];
	case 2: return regs.wordregs[regdx];
	case 3: return regs.wordregs[regbx];
	case 4: return regs.wordregs[regsp];
	case 5: return regs.wordregs[regbp];
	case 6: return regs.wordregs[regsi];
	case 7: return regs.wordregs[regdi];
	}
}

FUNC_INLINE uint32_t getreg32(uint8_t reg) {
	switch (reg) {
	case 0: return regs.longregs[regeax];
	case 1: return regs.longregs[regecx];
	case 2: return regs.longregs[regedx];
	case 3: return regs.longregs[regebx];
	case 4: return regs.longregs[regesp];
	case 5: return regs.longregs[regebp];
	case 6: return regs.longregs[regesi];
	case 7: return regs.longregs[regedi];
	}
}

FUNC_INLINE void putreg16(uint8_t reg, uint16_t writeval) {
	switch (reg) {
	case 0: regs.wordregs[regax] = writeval; break;
	case 1: regs.wordregs[regcx] = writeval; break;
	case 2: regs.wordregs[regdx] = writeval; break;
	case 3: regs.wordregs[regbx] = writeval; break;
	case 4: regs.wordregs[regsp] = writeval; break;
	case 5: regs.wordregs[regbp] = writeval; break;
	case 6: regs.wordregs[regsi] = writeval; break;
	case 7: regs.wordregs[regdi] = writeval; break;
	}
}

FUNC_INLINE void putreg32(uint8_t reg, uint32_t writeval) {
	switch (reg) {
	case 0: regs.longregs[regeax] = writeval; break;
	case 1: regs.longregs[regecx] = writeval; break;
	case 2: regs.longregs[regedx] = writeval; break;
	case 3: regs.longregs[regebx] = writeval; break;
	case 4: regs.longregs[regesp] = writeval; break;
	case 5: regs.longregs[regebp] = writeval; break;
	case 6: regs.longregs[regesi] = writeval; break;
	case 7: regs.longregs[regedi] = writeval; break;
	}
}

FUNC_INLINE uint32_t getsegreg(uint8_t reg) {
	switch (reg) {
	case 0: return segregs[reges];
	case 1: return segregs[regcs];
	case 2: return segregs[regss];
	case 3: return segregs[regds];
	case 4: return segregs[regfs];
	case 5: return segregs[reggs];
	}
}

FUNC_INLINE void putsegreg(uint8_t reg, uint32_t writeval) {
	uint32_t addr, gdtidx;
	uint16_t seg;

	if (protected && !v86f) { //TODO: is this logic right?
		uint8_t fault = 0;
		seg = writeval & ~7;
		if (((uint32_t)seg + 7) > ((writeval & 4) ? ldtl : gdtl)) { //Selector outside table limit
			fault = 1;
			if (showops) debug_log(DEBUG_DETAIL, "Selector %04X offset outside %s limit (%lu > %lu)\n", writeval, (writeval & 4) ? "LDT" : "GDT", (uint32_t)seg, (writeval & 4) ? ldtl : gdtl);
		}
		else {
			gdtidx = ((writeval & 4) ? ldtr : gdtr) + (uint32_t)seg;
			if ((cpu_read(gdtidx + 5) & 0x80) == 0) {
				fault = 1; //Present flag not set
				if (showops) debug_log(DEBUG_DETAIL, "Present flag for selector %04X in %s not set\n", seg, (writeval & 4) ? "LDT" : "GDT");
			}
		}
		if (fault && (reg == regcs)) {
			debug_log(DEBUG_DETAIL, "RAISE GP EXCEPTION\n");
			exception(13, writeval & 0xFFFC); //GP
			return;
		}
		segis32[reg] = (cpu_read(gdtidx + 6) >> 6) & 1;
		seglimit[reg] = (uint32_t)cpu_readw(gdtidx) | ((uint32_t)(cpu_read(gdtidx + 6) & 0xF) << 16);
		if (showops) debug_log(DEBUG_DETAIL, "Segment %04X is %s\n", writeval >> 3, segis32[reg] ? "32-bit" : "16-bit");
		if (cpu_read(gdtidx + 6) & 0x80) {
			seglimit[reg] <<= 12;
			seglimit[reg] |= 0xFFF;
		}
	}
	else {
		segis32[reg] = 0;
	}

	switch (reg) {
	case 0:
		segregs[reges] = writeval;
		segcache[reges] = segtolinear(writeval);
		break;
	case 1:
		segregs[regcs] = writeval;
		segcache[regcs] = segtolinear(writeval);
		if (protected) {
			//debug_log(DEBUG_DETAIL, "Changing CS in protected mode to %04X (selector %04X)\n", writeval, seg);
			if (!v86f) {
				cpl = writeval & 0x03;
			}
			else {
				cpl = 3;
			}
			//if (isoper32) debug_log(DEBUG_DETAIL, "Entered 32-bit CS segment\n");
		}
		break;
	case 2:
		segregs[regss] = writeval;
		segcache[regss] = segtolinear(writeval);
		break;
	case 3:
		segregs[regds] = writeval;
		segcache[regds] = segtolinear(writeval);
		break;
	case 4:
		segregs[regfs] = writeval;
		segcache[regfs] = segtolinear(writeval);
		break;
	case 5:
		segregs[reggs] = writeval;
		segcache[reggs] = segtolinear(writeval);
		break;
	}
}

FUNC_INLINE void flag_szp8(uint8_t value) {
	if (!value) {
		zf = 1;
	}
	else {
		zf = 0;	/* set or clear zero flag */
	}

	if (value & 0x80) {
		sf = 1;
	}
	else {
		sf = 0;	/* set or clear sign flag */
	}

	pf = parity[value]; /* retrieve parity state from lookup table */
}

FUNC_INLINE void flag_szp16(uint16_t value) {
	if (!value) {
		zf = 1;
	}
	else {
		zf = 0;	/* set or clear zero flag */
	}

	if (value & 0x8000) {
		sf = 1;
	}
	else {
		sf = 0;	/* set or clear sign flag */
	}

	pf = parity[value & 255];	/* retrieve parity state from lookup table */
}

FUNC_INLINE void flag_szp32(uint32_t value) {
	if (!value) {
		zf = 1;
	}
	else {
		zf = 0;	/* set or clear zero flag */
	}

	if (value & 0x80000000) {
		sf = 1;
	}
	else {
		sf = 0;	/* set or clear sign flag */
	}

	pf = parity[value & 255];	/* retrieve parity state from lookup table */
}

FUNC_INLINE void flag_log8(uint8_t value) {
	flag_szp8(value);
	cf = 0;
	of = 0; /* bitwise logic ops always clear carry and overflow */
}

FUNC_INLINE void flag_log16(uint16_t value) {
	flag_szp16(value);
	cf = 0;
	of = 0; /* bitwise logic ops always clear carry and overflow */
}

FUNC_INLINE void flag_log32(uint32_t value) {
	flag_szp32(value);
	cf = 0;
	of = 0; /* bitwise logic ops always clear carry and overflow */
}

FUNC_INLINE void flag_adc8(uint8_t v1, uint8_t v2, uint8_t v3) {

	/* v1 = destination operand, v2 = source operand, v3 = carry flag */
	uint16_t	dst;

	dst = (uint16_t)v1 + (uint16_t)v2 + (uint16_t)v3;
	flag_szp8((uint8_t)dst);
	if (((dst ^ v1) & (dst ^ v2) & 0x80) == 0x80) {
		of = 1;
	}
	else {
		of = 0; /* set or clear overflow flag */
	}

	if (dst & 0xFF00) {
		cf = 1;
	}
	else {
		cf = 0; /* set or clear carry flag */
	}

	if (((v1 ^ v2 ^ dst) & 0x10) == 0x10) {
		af = 1;
	}
	else {
		af = 0; /* set or clear auxilliary flag */
	}
}

FUNC_INLINE void flag_adc16(uint16_t v1, uint16_t v2, uint16_t v3) {

	uint32_t	dst;

	dst = (uint32_t)v1 + (uint32_t)v2 + (uint32_t)v3;
	flag_szp16((uint16_t)dst);
	if ((((dst ^ v1) & (dst ^ v2)) & 0x8000) == 0x8000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if (dst & 0xFFFF0000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if (((v1 ^ v2 ^ dst) & 0x10) == 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_adc32(uint32_t v1, uint32_t v2, uint32_t v3) {

	uint64_t	dst;

	dst = (uint64_t)v1 + (uint64_t)v2 + (uint64_t)v3;
	flag_szp32((uint32_t)dst);
	if ((((dst ^ v1) & (dst ^ v2)) & 0x80000000) == 0x80000000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if (dst & 0xFFFFFFFF00000000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if (((v1 ^ v2 ^ dst) & 0x10) == 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_add8(uint8_t v1, uint8_t v2) {
	/* v1 = destination operand, v2 = source operand */
	uint16_t	dst;

	dst = (uint16_t)v1 + (uint16_t)v2;
	flag_szp8((uint8_t)dst);
	if (dst & 0xFF00) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if (((dst ^ v1) & (dst ^ v2) & 0x80) == 0x80) {
		of = 1;
	}
	else {
		of = 0;
	}

	if (((v1 ^ v2 ^ dst) & 0x10) == 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_add16(uint16_t v1, uint16_t v2) {
	/* v1 = destination operand, v2 = source operand */
	uint32_t	dst;

	dst = (uint32_t)v1 + (uint32_t)v2;
	flag_szp16((uint16_t)dst);
	if (dst & 0xFFFF0000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if (((dst ^ v1) & (dst ^ v2) & 0x8000) == 0x8000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if (((v1 ^ v2 ^ dst) & 0x10) == 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_add32(uint32_t v1, uint32_t v2) {
	/* v1 = destination operand, v2 = source operand */
	uint64_t	dst;

	dst = (uint64_t)v1 + (uint64_t)v2;
	flag_szp32((uint32_t)dst);
	if (dst & 0xFFFFFFFF00000000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if (((dst ^ v1) & (dst ^ v2) & 0x80000000) == 0x80000000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if (((v1 ^ v2 ^ dst) & 0x10) == 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_sbb8(uint8_t v1, uint8_t v2, uint8_t v3) {

	/* v1 = destination operand, v2 = source operand, v3 = carry flag */
	uint16_t	dst;

	v2 += v3;
	dst = (uint16_t)v1 - (uint16_t)v2;
	flag_szp8((uint8_t)dst);
	if (dst & 0xFF00) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if ((dst ^ v1) & (v1 ^ v2) & 0x80) {
		of = 1;
	}
	else {
		of = 0;
	}

	if ((v1 ^ v2 ^ dst) & 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_sbb16(uint16_t v1, uint16_t v2, uint16_t v3) {

	/* v1 = destination operand, v2 = source operand, v3 = carry flag */
	uint32_t	dst;

	v2 += v3;
	dst = (uint32_t)v1 - (uint32_t)v2;
	flag_szp16((uint16_t)dst);
	if (dst & 0xFFFF0000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if ((dst ^ v1) & (v1 ^ v2) & 0x8000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if ((v1 ^ v2 ^ dst) & 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_sbb32(uint32_t v1, uint32_t v2, uint32_t v3) {

	/* v1 = destination operand, v2 = source operand, v3 = carry flag */
	uint64_t	dst;

	v2 += v3;
	dst = (uint64_t)v1 - (uint64_t)v2;
	flag_szp32((uint32_t)dst);
	if (dst & 0xFFFFFFFF00000000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if ((dst ^ v1) & (v1 ^ v2) & 0x80000000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if ((v1 ^ v2 ^ dst) & 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_sub8(uint8_t v1, uint8_t v2) {

	/* v1 = destination operand, v2 = source operand */
	uint16_t	dst;

	dst = (uint16_t)v1 - (uint16_t)v2;
	flag_szp8((uint8_t)dst);
	if (dst & 0xFF00) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if ((dst ^ v1) & (v1 ^ v2) & 0x80) {
		of = 1;
	}
	else {
		of = 0;
	}

	if ((v1 ^ v2 ^ dst) & 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_sub16(uint16_t v1, uint16_t v2) {

	/* v1 = destination operand, v2 = source operand */
	uint32_t	dst;

	dst = (uint32_t)v1 - (uint32_t)v2;
	flag_szp16((uint16_t)dst);
	if (dst & 0xFFFF0000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if ((dst ^ v1) & (v1 ^ v2) & 0x8000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if ((v1 ^ v2 ^ dst) & 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void flag_sub32(uint32_t v1, uint32_t v2) {

	/* v1 = destination operand, v2 = source operand */
	uint64_t	dst;

	dst = (uint64_t)v1 - (uint64_t)v2;
	flag_szp32((uint32_t)dst);
	if (dst & 0xFFFFFFFF00000000) {
		cf = 1;
	}
	else {
		cf = 0;
	}

	if ((dst ^ v1) & (v1 ^ v2) & 0x80000000) {
		of = 1;
	}
	else {
		of = 0;
	}

	if ((v1 ^ v2 ^ dst) & 0x10) {
		af = 1;
	}
	else {
		af = 0;
	}
}

FUNC_INLINE void op_adc8() {
	res8 = oper1b + oper2b + cf;
	flag_adc8(oper1b, oper2b, cf);
}

FUNC_INLINE void op_adc16() {
	res16 = oper1 + oper2 + cf;
	flag_adc16(oper1, oper2, cf);
}

FUNC_INLINE void op_adc32() {
	res32 = oper1_32 + oper2_32 + (uint32_t)cf;
	flag_adc32(oper1_32, oper2_32, cf);
}

FUNC_INLINE void op_add8() {
	res8 = oper1b + oper2b;
	flag_add8(oper1b, oper2b);
}

FUNC_INLINE void op_add16() {
	res16 = oper1 + oper2;
	flag_add16(oper1, oper2);
}

FUNC_INLINE void op_add32() {
	res32 = oper1_32 + oper2_32;
	flag_add32(oper1_32, oper2_32);
}

FUNC_INLINE void op_and8() {
	res8 = oper1b & oper2b;
	flag_log8(res8);
}

FUNC_INLINE void op_and16() {
	res16 = oper1 & oper2;
	flag_log16(res16);
}

FUNC_INLINE void op_and32() {
	res32 = oper1_32 & oper2_32;
	flag_log32(res32);
}

FUNC_INLINE void op_or8() {
	res8 = oper1b | oper2b;
	flag_log8(res8);
}

FUNC_INLINE void op_or16() {
	res16 = oper1 | oper2;
	flag_log16(res16);
}

FUNC_INLINE void op_or32() {
	res32 = oper1_32 | oper2_32;
	flag_log32(res32);
}

FUNC_INLINE void op_xor8() {
	res8 = oper1b ^ oper2b;
	flag_log8(res8);
}

FUNC_INLINE void op_xor16() {
	res16 = oper1 ^ oper2;
	flag_log16(res16);
}

FUNC_INLINE void op_xor32() {
	res32 = oper1_32 ^ oper2_32;
	flag_log32(res32);
}

FUNC_INLINE void op_sub8() {
	res8 = oper1b - oper2b;
	flag_sub8(oper1b, oper2b);
}

FUNC_INLINE void op_sub16() {
	res16 = oper1 - oper2;
	flag_sub16(oper1, oper2);
}

FUNC_INLINE void op_sub32() {
	res32 = oper1_32 - oper2_32;
	flag_sub32(oper1_32, oper2_32);
}

FUNC_INLINE void op_sbb8() {
	res8 = oper1b - (oper2b + cf);
	flag_sbb8(oper1b, oper2b, cf);
}

FUNC_INLINE void op_sbb16() {
	res16 = oper1 - (oper2 + cf);
	flag_sbb16(oper1, oper2, cf);
}

FUNC_INLINE void op_sbb32() {
	res32 = oper1_32 - (oper2_32 + (uint32_t)cf);
	flag_sbb32(oper1_32, oper2_32, cf);
}

FUNC_INLINE void getea(uint8_t rmval) {
	uint32_t	tempea;

	tempea = 0;
	if (!isaddr32) {
		switch (mode) {
		case 0:
			switch (rmval) {
			case 0:
				tempea = regs.wordregs[regbx] + regs.wordregs[regsi];
				break;
			case 1:
				tempea = regs.wordregs[regbx] + regs.wordregs[regdi];
				break;
			case 2:
				tempea = regs.wordregs[regbp] + regs.wordregs[regsi];
				break;
			case 3:
				tempea = regs.wordregs[regbp] + regs.wordregs[regdi];
				break;
			case 4:
				tempea = regs.wordregs[regsi];
				break;
			case 5:
				tempea = regs.wordregs[regdi];
				break;
			case 6:
				tempea = disp16; // (int32_t)(int16_t)disp16;
				break;
			case 7:
				tempea = regs.wordregs[regbx];
				break;
			}
			break;

		case 1:
		case 2:
			switch (rmval) {
			case 0:
				tempea = regs.wordregs[regbx] + regs.wordregs[regsi] + disp16;
				break;
			case 1:
				tempea = regs.wordregs[regbx] + regs.wordregs[regdi] + disp16;
				break;
			case 2:
				tempea = regs.wordregs[regbp] + regs.wordregs[regsi] + disp16;
				break;
			case 3:
				tempea = regs.wordregs[regbp] + regs.wordregs[regdi] + disp16;
				break;
			case 4:
				tempea = regs.wordregs[regsi] + disp16;
				break;
			case 5:
				tempea = regs.wordregs[regdi] + disp16;
				break;
			case 6:
				tempea = regs.wordregs[regbp] + disp16;
				break;
			case 7:
				tempea = regs.wordregs[regbx] + disp16;
				break;
			}
			break;
		}

		ea = (tempea & 0xFFFF) + useseg; // (useseg << 4);
	}
	else { //32-bit addressing
		switch (mode) {
		case 0:
			switch (rmval) {
			case 0:
				tempea = regs.longregs[regeax];
				break;
			case 1:
				tempea = regs.longregs[regecx];
				break;
			case 2:
				tempea = regs.longregs[regedx];
				break;
			case 3:
				tempea = regs.longregs[regebx];
				break;
			case 4:
				tempea = sib_val;
				if (sib_base == 5) tempea += disp32;
				break;
			case 5:
				tempea = disp32;
				break;
			case 6:
				tempea = regs.longregs[regesi];
				break;
			case 7:
				tempea = regs.longregs[regedi];
				break;
			}
			break;

		case 1:
		case 2:
			switch (rmval) {
			case 0:
				tempea = regs.longregs[regeax] + disp32;
				break;
			case 1:
				tempea = regs.longregs[regecx] + disp32;
				break;
			case 2:
				tempea = regs.longregs[regedx] + disp32;
				break;
			case 3:
				tempea = regs.longregs[regebx] + disp32;
				break;
			case 4:
				tempea = sib_val + disp32;
				break;
			case 5:
				tempea = regs.longregs[regebp] + disp32;
				break;
			case 6:
				tempea = regs.longregs[regesi] + disp32;
				break;
			case 7:
				tempea = regs.longregs[regedi] + disp32;
				break;
			}
			break;
		}

		if (protected && !v86f) {
			ea = tempea + useseg;
		}
		else {
			ea = (tempea & 0xFFFF) + useseg;
		}
	}
}

FUNC_INLINE void pushw(uint16_t pushval) {
	if (showops) debug_log(DEBUG_DETAIL, "PUSH %04X\n", pushval);

	if (segis32[regss]) {
		regs.longregs[regesp] -= 2;
		putmem16(segcache[regss], regs.longregs[regesp], pushval);
	}
	else {
		regs.wordregs[regsp] -= 2;
		putmem16(segcache[regss], regs.wordregs[regsp], pushval);
	}
}

FUNC_INLINE void pushl(uint32_t pushval) {
	if (showops) debug_log(DEBUG_DETAIL, "PUSH %08X\n", pushval);

	if (segis32[regss]) {
		regs.longregs[regesp] -= 4;
		putmem32(segcache[regss], regs.longregs[regesp], pushval);
	}
	else {
		regs.wordregs[regsp] -= 4;
		putmem32(segcache[regss], regs.wordregs[regsp], pushval);
	}
}

FUNC_INLINE void push(uint32_t pushval) {
	if (showops) debug_log(DEBUG_DETAIL, "PUSH %08X\n", pushval);

	if (segis32[regss]) {
		if (isoper32) {
			regs.longregs[regesp] -= 4;
			putmem32(segcache[regss], regs.longregs[regesp], pushval);
		}
		else {
			regs.longregs[regesp] -= 2;
			putmem16(segcache[regss], regs.longregs[regesp], pushval & 0xFFFF);
		}
	}
	else {
		if (isoper32) {
			regs.wordregs[regsp] -= 4;
			putmem32(segcache[regss], regs.wordregs[regsp], pushval);
		}
		else {
			regs.wordregs[regsp] -= 2;
			putmem16(segcache[regss], regs.wordregs[regsp], pushval & 0xFFFF);
		}
	}
}

FUNC_INLINE uint16_t popw() {
	uint16_t tempval;

	if (segis32[regss]) {
		tempval = getmem16(segcache[regss], regs.longregs[regesp]);
		regs.longregs[regesp] += 2;
	}
	else {
		tempval = getmem16(segcache[regss], regs.wordregs[regsp]);
		regs.wordregs[regsp] += 2;
	}

	if (showops) debug_log(DEBUG_DETAIL, "POP %04X\n", tempval);
	return tempval;
}

FUNC_INLINE uint32_t popl() {
	uint32_t tempval;

	if (segis32[regss]) {
		tempval = getmem32(segcache[regss], regs.longregs[regesp]);
		regs.longregs[regesp] += 4;
	}
	else {
		tempval = getmem32(segcache[regss], regs.wordregs[regsp]);
		regs.wordregs[regsp] += 4;
	}

	if (showops) debug_log(DEBUG_DETAIL, "POP %08X\n", tempval);
	return tempval;
}

FUNC_INLINE uint32_t pop() {
	uint32_t tempval;

	if (segis32[regss]) {
		if (isoper32) {
			tempval = getmem32(segcache[regss], regs.longregs[regesp]);
			regs.longregs[regesp] += 4;
		}
		else {
			tempval = getmem16(segcache[regss], regs.longregs[regesp]);
			regs.longregs[regesp] += 2;
		}
	}
	else {
		if (isoper32) {
			tempval = getmem32(segcache[regss], regs.wordregs[regsp]);
			regs.wordregs[regsp] += 4;
		}
		else {
			tempval = getmem16(segcache[regss], regs.wordregs[regsp]);
			regs.wordregs[regsp] += 2;
		}
	}

	if (showops) debug_log(DEBUG_DETAIL, "POP %08X\n", tempval);
	return tempval;
}

void reset86() {
	uint16_t i;
	static uint8_t firstreset = 1;
	if (firstreset) for (i = 0; i < 256; i++) {
		int_callback[i] = NULL;
	}
	firstreset = 0;
	usegdt = 0;
	protected = 0;
	paging = 0;
	memset(segis32, 0, sizeof(segis32));
	memset(HMA, 0, sizeof(HMA));
	a20_gate = 0;
	putsegreg(regcs, 0xFFFF);
	ip = 0x0000;
	hltstate = 0;
	trap_toggle = 0;
	have387 = 1;
	cr[0] = 0x00000010 | ((have387 ^ 1) << 2);
}

FUNC_INLINE uint16_t readrm16(uint8_t rmval) {
	if (mode < 3) {
		getea(rmval);
		return cpu_read(ea) | ((uint16_t)cpu_read(ea + 1) << 8);
	}
	else {
		return getreg16(rmval);
	}
}

FUNC_INLINE uint32_t readrm32(uint8_t rmval) {
	if (mode < 3) {
		getea(rmval);
		return cpu_read(ea) | ((uint32_t)cpu_read(ea + 1) << 8) | ((uint32_t)cpu_read(ea + 2) << 16) | ((uint32_t)cpu_read(ea + 3) << 24);
	}
	else {
		return getreg32(rmval);
	}
}

FUNC_INLINE uint64_t readrm64(uint8_t rmval) {
	if (mode < 3) {
		getea(rmval);
		return cpu_read(ea) | ((uint64_t)cpu_read(ea + 1) << 8) | ((uint64_t)cpu_read(ea + 2) << 16) | ((uint64_t)cpu_read(ea + 3) << 24) |
			((uint64_t)cpu_read(ea + 4) << 32) | ((uint64_t)cpu_read(ea + 5) << 40) | ((uint64_t)cpu_read(ea + 6) << 48) | ((uint64_t)cpu_read(ea + 7) << 56);
	}
	else {
		return getreg32(rmval);
	}
}

FUNC_INLINE uint8_t readrm8(uint8_t rmval) {
	if (mode < 3) {
		getea(rmval);
		return cpu_read(ea);
	}
	else {
		return getreg8(rmval);
	}
}

FUNC_INLINE void writerm32(uint8_t rmval, uint32_t value) {
	if (mode < 3) {
		getea(rmval);
		cpu_write(ea, value & 0xFF);
		cpu_write(ea + 1, value >> 8);
		cpu_write(ea + 2, value >> 16);
		cpu_write(ea + 3, value >> 24);
	}
	else {
		putreg32(rmval, value);
	}
}

FUNC_INLINE void writerm64(uint8_t rmval, uint64_t value) {
	if (mode < 3) {
		getea(rmval);
		cpu_write(ea, value & 0xFF);
		cpu_write(ea + 1, value >> 8);
		cpu_write(ea + 2, value >> 16);
		cpu_write(ea + 3, value >> 24);
		cpu_write(ea + 4, value >> 32);
		cpu_write(ea + 5, value >> 40);
		cpu_write(ea + 6, value >> 48);
		cpu_write(ea + 7, value >> 56);
	}
	else {
		putreg32(rmval, value);
	}
}

FUNC_INLINE void writerm16(uint8_t rmval, uint16_t value) {
	if (mode < 3) {
		getea(rmval);
		cpu_write(ea, value & 0xFF);
		cpu_write(ea + 1, value >> 8);
	}
	else {
		putreg16(rmval, value);
	}
}

FUNC_INLINE void writerm8(uint8_t rmval, uint8_t value) {
	if (mode < 3) {
		getea(rmval);
		cpu_write(ea, value);
	}
	else {
		putreg8(rmval, value);
	}
}

FUNC_INLINE uint8_t op_grp2_8(uint8_t cnt) {

	uint16_t	s;
	uint16_t	shift;
	uint16_t	oldcf;
	uint16_t	msb;

	s = oper1b;
	oldcf = cf;
	cnt &= 0x1F;
	switch (reg) {
	case 0: /* ROL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			if (s & 0x80) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = s << 1;
			s = s | cf;
		}

		if (cnt == 1) {
			if ((s & 0x80) && cf) of = 1; else of = 0;
			//of = ((s >> 7) ^ ((s >> 6) & 1)) & 1; //TODO: correct, or go back to line above?
		}
		else of = 0;
		break;

	case 1: /* ROR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			cf = s & 1;
			s = (s >> 1) | (cf << 7);
		}

		if (cnt == 1) {
			of = (s >> 7) ^ ((s >> 6) & 1);
		}
		break;

	case 2: /* RCL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			oldcf = cf;
			if (s & 0x80) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = s << 1;
			s = s | oldcf;
		}

		if (cnt == 1) {
			of = cf ^ ((s >> 7) & 1);
		}
		break;

	case 3: /* RCR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			oldcf = cf;
			cf = s & 1;
			s = (s >> 1) | (oldcf << 7);
		}

		if (cnt == 1) {
			of = (s >> 7) ^ ((s >> 6) & 1);
		}
		break;

	case 4:
	case 6: /* SHL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			if (s & 0x80) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = (s << 1) & 0xFF;
		}

		if (cnt == 1) { //((cnt == 1) && (cf == (s >> 7))) {
			of = ((s >> 7) ^ cf) & 1; //0;
		}
		/*else {
			of = 1;
		}*/

		flag_szp8((uint8_t)s);
		break;

	case 5: /* SHR r/m8 */
		if ((cnt == 1) && (s & 0x80)) {
			of = 1;
		}
		else {
			of = 0;
		}

		for (shift = 1; shift <= cnt; shift++) {
			cf = s & 1;
			s = s >> 1;
		}

		flag_szp8((uint8_t)s);
		break;

	case 7: /* SAR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			msb = s & 0x80;
			cf = s & 1;
			s = (s >> 1) | msb;
		}

		of = 0;
		flag_szp8((uint8_t)s);
		break;
	}

	return s & 0xFF;
}

FUNC_INLINE uint16_t op_grp2_16(uint8_t cnt) {

	uint32_t	s;
	uint32_t	shift;
	uint32_t	oldcf;
	uint32_t	msb;

	s = oper1;
	oldcf = cf;
	cnt &= 0x1F;
	switch (reg) {
	case 0: /* ROL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			if (s & 0x8000) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = s << 1;
			s = s | cf;
		}

		if (cnt == 1) {
			of = cf ^ ((s >> 15) & 1);
		}
		break;

	case 1: /* ROR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			cf = s & 1;
			s = (s >> 1) | (cf << 15);
		}

		if (cnt == 1) {
			of = (s >> 15) ^ ((s >> 14) & 1);
		}
		break;

	case 2: /* RCL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			oldcf = cf;
			if (s & 0x8000) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = s << 1;
			s = s | oldcf;
		}

		if (cnt == 1) {
			of = cf ^ ((s >> 15) & 1);
		}
		break;

	case 3: /* RCR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			oldcf = cf;
			cf = s & 1;
			s = (s >> 1) | (oldcf << 15);
		}

		if (cnt == 1) {
			of = (s >> 15) ^ ((s >> 14) & 1);
		}
		break;

	case 4:
	case 6: /* SHL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			if (s & 0x8000) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = (s << 1) & 0xFFFF;
		}

		if (cnt == 1) { //((cnt == 1) && (cf == (s >> 15))) {
			of = ((s >> 15) ^ cf) & 1; //0;
		}
		/*else {
			of = 1;
		}*/

		flag_szp16((uint16_t)s);
		break;

	case 5: /* SHR r/m8 */
		if ((cnt == 1) && (s & 0x8000)) {
			of = 1;
		}
		else {
			of = 0;
		}

		for (shift = 1; shift <= cnt; shift++) {
			cf = s & 1;
			s = s >> 1;
		}

		flag_szp16((uint16_t)s);
		break;

	case 7: /* SAR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			msb = s & 0x8000;
			cf = s & 1;
			s = (s >> 1) | msb;
		}

		of = 0;
		flag_szp16((uint16_t)s);
		break;
	}

	return (uint16_t)s & 0xFFFF;
}

FUNC_INLINE uint32_t op_grp2_32(uint8_t cnt) {

	uint64_t	s;
	uint64_t	shift;
	uint64_t	oldcf;
	uint64_t	msb;

	s = oper1_32;
	oldcf = cf;
	cnt &= 0x1F;
	switch (reg) {
	case 0: /* ROL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			if (s & 0x80000000) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = s << 1;
			s = s | cf;
		}

		if (cnt == 1) {
			of = cf ^ ((s >> 31) & 1);
		}
		break;

	case 1: /* ROR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			cf = s & 1;
			s = (s >> 1) | ((uint64_t)cf << 31);
		}

		if (cnt == 1) {
			of = (s >> 31) ^ ((s >> 30) & 1);
		}
		break;

	case 2: /* RCL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			oldcf = cf;
			if (s & 0x80000000) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = s << 1;
			s = s | oldcf;
		}

		if (cnt == 1) {
			of = cf ^ ((s >> 31) & 1);
		}
		break;

	case 3: /* RCR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			oldcf = cf;
			cf = s & 1;
			s = (s >> 1) | (oldcf << 31);
		}

		if (cnt == 1) {
			of = (s >> 31) ^ ((s >> 30) & 1);
		}
		break;

	case 4:
	case 6: /* SHL r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			if (s & 0x80000000) {
				cf = 1;
			}
			else {
				cf = 0;
			}

			s = (s << 1) & 0xFFFFFFFF;
		}

		//if ((cnt == 1) && (cf == (s >> 31))) {
		if (cnt == 1) {
			of = ((s >> 31) ^ cf) & 1; //0;
		}
		/*else {
			of = 1;
		}*/

		flag_szp32((uint32_t)s);
		break;

	case 5: /* SHR r/m8 */
		if ((cnt == 1) && (s & 0x80000000)) {
			of = 1;
		}
		else {
			of = 0;
		}

		for (shift = 1; shift <= cnt; shift++) {
			cf = s & 1;
			s = s >> 1;
		}

		flag_szp32((uint32_t)s);
		break;

	case 7: /* SAR r/m8 */
		for (shift = 1; shift <= cnt; shift++) {
			msb = s & 0x80000000;
			cf = s & 1;
			s = (s >> 1) | msb;
		}

		of = 0;
		flag_szp32((uint32_t)s);
		break;
	}

	return (uint32_t)s;
}

FUNC_INLINE void op_div8(uint16_t valdiv, uint8_t divisor) {
	if (divisor == 0) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	if ((valdiv / (uint16_t)divisor) > 0xFF) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	regs.byteregs[regah] = valdiv % (uint16_t)divisor;
	regs.byteregs[regal] = valdiv / (uint16_t)divisor;
}

FUNC_INLINE void op_idiv8(uint16_t valdiv, uint8_t divisor) {
	//TODO: Rewrite IDIV code, I wrote this in 2011. It can be made far more efficient.
	uint16_t	s1;
	uint16_t	s2;
	uint16_t	d1;
	uint16_t	d2;
	int	sign;

	if (divisor == 0) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	s1 = valdiv;
	s2 = divisor;
	sign = (((s1 ^ s2) & 0x8000) != 0);
	s1 = (s1 < 0x8000) ? s1 : ((~s1 + 1) & 0xffff);
	s2 = (s2 < 0x8000) ? s2 : ((~s2 + 1) & 0xffff);
	d1 = s1 / s2;
	d2 = s1 % s2;
	if (d1 & 0xFF00) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	if (sign) {
		d1 = (~d1 + 1) & 0xff;
		d2 = (~d2 + 1) & 0xff;
	}

	regs.byteregs[regah] = (uint8_t)d2;
	regs.byteregs[regal] = (uint8_t)d1;
}

FUNC_INLINE void op_grp3_8() {
	//oper1 = signext(oper1b);
	//oper2 = signext(oper2b);
	switch (reg) {
	case 0:
	case 1: /* TEST */
		flag_log8(oper1b & getmem8(segcache[regcs], ip));
		StepIP(1);
		break;

	case 2: /* NOT */
		res8 = ~oper1b;
		break;

	case 3: /* NEG */
		res8 = (~oper1b) + 1;
		flag_sub8(0, oper1b);
		if (res8 == 0) {
			cf = 0;
		}
		else {
			cf = 1;
		}
		break;

	case 4: /* MUL */
		temp1 = (uint32_t)oper1b * (uint32_t)regs.byteregs[regal];
		regs.wordregs[regax] = temp1 & 0xFFFF;
		//flag_szp8((uint8_t)temp1); //TODO: undefined?
		if (regs.byteregs[regah]) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		break;

	case 5: /* IMUL */
		oper1 = signext(oper1b);
		temp1 = signext(regs.byteregs[regal]);
		temp2 = oper1;
		if ((temp1 & 0x80) == 0x80) {
			temp1 = temp1 | 0xFFFFFF00;
		}

		if ((temp2 & 0x80) == 0x80) {
			temp2 = temp2 | 0xFFFFFF00;
		}

		temp3 = (temp1 * temp2) & 0xFFFF;
		regs.wordregs[regax] = temp3 & 0xFFFF;
		if (regs.byteregs[regah]) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		break;

	case 6: /* DIV */
		op_div8(regs.wordregs[regax], oper1b);
		break;

	case 7: /* IDIV */
		op_idiv8(regs.wordregs[regax], oper1b);
		break;
	}
}

FUNC_INLINE void op_div16(uint32_t valdiv, uint16_t divisor) {
	if (divisor == 0) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	if ((valdiv / (uint32_t)divisor) > 0xFFFF) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	regs.wordregs[regdx] = valdiv % (uint32_t)divisor;
	regs.wordregs[regax] = valdiv / (uint32_t)divisor;
}

FUNC_INLINE void op_div32(uint64_t valdiv, uint32_t divisor) {
	if (divisor == 0) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	if ((valdiv / (uint64_t)divisor) > 0xFFFFFFFF) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	regs.longregs[regedx] = valdiv % (uint32_t)divisor;
	regs.longregs[regeax] = valdiv / (uint32_t)divisor;
}

FUNC_INLINE void op_idiv16(uint32_t valdiv, uint16_t divisor) {
	//TODO: Rewrite IDIV code, I wrote this in 2011. It can be made far more efficient.
	uint32_t	d1;
	uint32_t	d2;
	uint32_t	s1;
	uint32_t	s2;
	int	sign;

	if (divisor == 0) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	s1 = valdiv;
	s2 = divisor;
	s2 = (s2 & 0x8000) ? (s2 | 0xffff0000) : s2;
	sign = (((s1 ^ s2) & 0x80000000) != 0);
	s1 = (s1 < 0x80000000) ? s1 : ((~s1 + 1) & 0xffffffff);
	s2 = (s2 < 0x80000000) ? s2 : ((~s2 + 1) & 0xffffffff);
	d1 = s1 / s2;
	d2 = s1 % s2;
	if (d1 & 0xFFFF0000) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	if (sign) {
		d1 = (~d1 + 1) & 0xffff;
		d2 = (~d2 + 1) & 0xffff;
	}

	regs.wordregs[regax] = d1;
	regs.wordregs[regdx] = d2;
}

FUNC_INLINE void op_idiv32(uint64_t valdiv, uint32_t divisor) {
	//TODO: Rewrite IDIV code, I wrote this in 2011. It can be made far more efficient.
	uint64_t	d1;
	uint64_t	d2;
	uint64_t	s1;
	uint64_t	s2;
	int	sign;

	if (divisor == 0) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	s1 = valdiv;
	s2 = divisor;
	s2 = (s2 & 0x80000000) ? (s2 | 0xffffffff00000000) : s2;
	sign = (((s1 ^ s2) & 0x8000000000000000) != 0);
	s1 = (s1 < 0x8000000000000000) ? s1 : ((~s1 + 1) & 0xffffffffffffffff);
	s2 = (s2 < 0x8000000000000000) ? s2 : ((~s2 + 1) & 0xffffffffffffffff);
	d1 = s1 / s2;
	d2 = s1 % s2;
	if (d1 & 0xFFFFFFFF00000000) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}

	if (sign) {
		d1 = (~d1 + 1) & 0xffffffff;
		d2 = (~d2 + 1) & 0xffffffff;
	}

	regs.longregs[regeax] = d1;
	regs.longregs[regedx] = d2;
}

FUNC_INLINE void op_grp3_16() {
	switch (reg) {
	case 0:
	case 1: /* TEST */
		flag_log16(oper1 & getmem16(segcache[regcs], ip));
		StepIP(2);
		break;

	case 2: /* NOT */
		res16 = ~oper1;
		break;

	case 3: /* NEG */
		res16 = (~oper1) + 1;
		flag_sub16(0, oper1);
		if (res16) {
			cf = 1;
		}
		else {
			cf = 0;
		}
		break;

	case 4: /* MUL */
		temp1 = (uint32_t)oper1 * (uint32_t)regs.wordregs[regax];
		regs.wordregs[regax] = temp1 & 0xFFFF;
		regs.wordregs[regdx] = temp1 >> 16;
		//flag_szp16((uint16_t)temp1); //TODO: undefined?
		if (regs.wordregs[regdx]) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		break;

	case 5: /* IMUL */
		temp1 = regs.wordregs[regax];
		temp2 = oper1;
		if (temp1 & 0x8000) {
			temp1 |= 0xFFFF0000;
		}

		if (temp2 & 0x8000) {
			temp2 |= 0xFFFF0000;
		}

		temp3 = temp1 * temp2;
		regs.wordregs[regax] = temp3 & 0xFFFF;	/* into register ax */
		regs.wordregs[regdx] = temp3 >> 16;	/* into register dx */
		if (regs.wordregs[regdx]) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		break;

	case 6: /* DIV */
		op_div16(((uint32_t)regs.wordregs[regdx] << 16) + regs.wordregs[regax], oper1);
		break;

	case 7: /* DIV */
		op_idiv16(((uint32_t)regs.wordregs[regdx] << 16) + regs.wordregs[regax], oper1);
		break;
	}
}

FUNC_INLINE void op_grp3_32() {
	switch (reg) {
	case 0:
	case 1: /* TEST */
		flag_log32(oper1_32 & getmem32(segcache[regcs], ip));
		StepIP(4);
		break;

	case 2: /* NOT */
		res32 = ~oper1_32;
		break;

	case 3: /* NEG */
		res32 = (~oper1_32) + 1;
		flag_sub32(0, oper1_32);
		if (res32) {
			cf = 1;
		}
		else {
			cf = 0;
		}
		break;

	case 4: /* MUL */
		temp64 = (uint64_t)oper1_32 * (uint64_t)regs.longregs[regeax];
		regs.longregs[regeax] = temp64 & 0xFFFFFFFF;
		regs.longregs[regedx] = temp64 >> 32;
		//flag_szp32((uint32_t)temp64); //TODO: undefined?
		if (regs.longregs[regedx]) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		break;

	case 5: /* IMUL */
		temp64 = regs.longregs[regeax];
		temp64_2 = oper1_32;
		if (temp64 & 0x80000000) {
			temp64 |= 0xFFFFFFFF00000000;
		}

		if (temp64_2 & 0x80000000) {
			temp64_2 |= 0xFFFFFFFF00000000;
		}

		temp64_3 = temp64 * temp64_2;
		regs.longregs[regeax] = temp64_3 & 0xFFFFFFFF;	/* into register ax */
		regs.longregs[regedx] = temp64_3 >> 32;	/* into register dx */
		if (regs.longregs[regedx]) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		break;

	case 6: /* DIV */
		op_div32(((uint64_t)regs.longregs[regedx] << 32) + regs.longregs[regeax], oper1_32);
		break;

	case 7: /* DIV */
		op_idiv32(((uint64_t)regs.longregs[regedx] << 32) + regs.longregs[regeax], oper1_32);
		break;
	}
}

FUNC_INLINE void op_grp5() {
	switch (reg) {
	case 0: /* INC Ev */
		oper2 = 1;
		tempcf = cf;
		op_add16();
		cf = tempcf;
		writerm16(rm, res16);
		break;

	case 1: /* DEC Ev */
		oper2 = 1;
		tempcf = cf;
		op_sub16();
		cf = tempcf;
		writerm16(rm, res16);
		break;

	case 2: /* CALL Ev */
		push(ip);
		ip = oper1;
		break;

	case 3: /* CALL Mp */
	{
		uint32_t new_ip;
		uint16_t new_cs;
		//push(segregs[regcs]);
		//push(ip);
		getea(rm);
		//ip = (uint16_t)cpu_read(ea) + (uint16_t)cpu_read(ea + 1) * 256;
		//putsegreg(regcs, (uint16_t)cpu_read(ea + 2) + (uint16_t)cpu_read(ea + 3) * 256);
		new_ip = (uint16_t)cpu_read(ea) + (uint16_t)cpu_read(ea + 1) * 256;
		new_cs = (uint16_t)cpu_read(ea + 2) + (uint16_t)cpu_read(ea + 3) * 256;
		cpu_callf(new_cs, new_ip);
		break;
	}

	case 4: /* JMP Ev */
		ip = oper1;
		break;

	case 5: /* JMP Mp */
		getea(rm);
		ip = (uint16_t)cpu_read(ea) + (uint16_t)cpu_read(ea + 1) * 256;
		putsegreg(regcs, (uint16_t)cpu_read(ea + 2) + (uint16_t)cpu_read(ea + 3) * 256);
		break;

	case 6: /* PUSH Ev */
		push(oper1);
		break;
	}
}

FUNC_INLINE void op_grp5_32() {
	//printf("op_grp5_32 reg = %u\n", reg);
	switch (reg) {
	case 0: /* INC Ev */
		oper2_32 = 1;
		tempcf = cf;
		op_add32();
		cf = tempcf;
		writerm32(rm, res32);
		break;

	case 1: /* DEC Ev */
		oper2_32 = 1;
		tempcf = cf;
		op_sub32();
		cf = tempcf;
		writerm32(rm, res32);
		break;

	case 2: /* CALL Ev */
		push(ip);
		ip = oper1_32;
		break;

	case 3: /* CALL Mp */ //TODO: is this right?
	{
		uint32_t new_ip;
		uint16_t new_cs;
		//push(segregs[regcs]);
		//push(ip);
		getea(rm);
		//ip = (uint32_t)cpu_read(ea) | ((uint32_t)cpu_read(ea + 1) << 8) | ((uint32_t)cpu_read(ea + 2) << 16) | ((uint32_t)cpu_read(ea + 3) << 24);
		//putsegreg(regcs, (uint16_t)cpu_read(ea + 4) | ((uint16_t)cpu_read(ea + 5) << 8));
		new_ip = (uint32_t)cpu_read(ea) | ((uint32_t)cpu_read(ea + 1) << 8) | ((uint32_t)cpu_read(ea + 2) << 16) | ((uint32_t)cpu_read(ea + 3) << 24);
		new_cs = (uint16_t)cpu_read(ea + 4) | ((uint16_t)cpu_read(ea + 5) << 8);
		cpu_callf(new_cs, new_ip);
		break;
	}

	case 4: /* JMP Ev */
		ip = oper1_32;
		//printf("GRP5_32 JMP EA = %08X, disp32 = %08X, mod = %u, reg = %u, rm = %u, SIB index = %u, SIB base = %u\n", ea, disp32, mode, reg, rm, sib_index, sib_base);
		break;

	case 5: /* JMP Mp */ //TODO: is this right?
		getea(rm);
		ip = (uint32_t)cpu_read(ea) | ((uint32_t)cpu_read(ea + 1) << 8) | ((uint32_t)cpu_read(ea + 2) << 16) | ((uint32_t)cpu_read(ea + 3) << 24);
		putsegreg(regcs, (uint16_t)cpu_read(ea + 4) | ((uint16_t)cpu_read(ea + 5) << 8));
		break;

	case 6: /* PUSH Ev */
		push(oper1_32);
		break;
	}
}

void cpu_int15_handler() {
	printf("Int 15h AX = %04X\n", regs.wordregs[regax]);
	switch (regs.byteregs[regah]) {
	case 0x24:
		switch (regs.byteregs[regal]) {
		case 0x00:
			a20_gate = 0;
			cf = 0;
			break;
		case 0x01:
			a20_gate = 1;
			cf = 0;
			break;
		case 0x02:
			regs.byteregs[regah] = 0;
			regs.byteregs[regal] = a20_gate;
			cf = 0;
			break;
		case 0x03:
			regs.byteregs[regah] = 0;
			regs.wordregs[regbx] = 1;
			cf = 0;
			break;
		}
		return;
	case 0x87:
	{
		uint32_t source, dest, len, table, i;
		uint8_t old_a20 = a20_gate;
		a20_gate = 1;
		table = (uint32_t)segregs[reges] * 16 + (uint32_t)regs.wordregs[regsi];
		source = cpu_readl(table + 0x12) & 0xFFFFFF;
		dest = cpu_readl(table + 0x1A) & 0xFFFFFF;
		len = (uint32_t)cpu_readw(table + 0x10) + 1; // (uint32_t)regs.wordregs[regcx] * 2;
		printf("Copy from %08X -> %08X (len %lu)\n", source, dest, len);
		for (i = 0; i < len; i++) {
			uint8_t val;
			val = cpu_read(source + i);
			cpu_write(dest + i, val);
			//printf("%c", val);
		}
		cf = 0;
		regs.byteregs[regah] = 0;
		a20_gate = old_a20;
		return;
	}
	case 0x88:
		cf = 0;
		regs.wordregs[regax] = (MEMORY_RANGE / 1024) - 1024;
		return;
	case 0x53:
		cf = 1;
		regs.byteregs[regah] = 0x86;
		break;
	default:
		printf("Other int 15h call: %02X\n", regs.byteregs[regah]);
		cf = 1;
	}
}

FUNC_INLINE void cpu_intcall(uint8_t intnum, uint8_t source, uint32_t err) {
    switch (intnum) {
        case 0x10: {
            switch (CPU_AH) {
                case 0x09:
                case 0x0a:
                    if (videomode >= 8 && videomode <= 0x13) {
                        // TODO: char attr?
                        tga_draw_char(CPU_AL, CURSOR_X, CURSOR_Y, 9);
                        printf("%c", CPU_AL);
                        return;
                    }
                    break;
                case 0x0f:
                    if (videomode < 8) break;
                    CPU_AL = videomode;
                    CPU_AH = 80;
                    CPU_BH = 0;
                    return;
                case 0x00:
                    // http://www.techhelpmanual.com/114-video_modes.html
                    // http://www.techhelpmanual.com/89-video_memory_layouts.html

                    videomode = CPU_AL & 0x7F;

                    RAM[0x449] = CPU_AL;
                    RAM[0x44A] = videomode <= 2 || (videomode >= 0x8 && videomode <= 0xa) ? 40 : 80;
                    RAM[0x44B] = 0;
                    RAM[0x484] = (25 - 1);

                    if ((CPU_AL & 0x80) == 0x00) {
                        memset(VIDEORAM, 0x0, VIDEORAM_SIZE);
                    }
                    vga_plane_offset = 0;
                    vga_planar_mode = 0;
                    tga_offset = 0x8000;
                    break;
                case 0x05: /* Select Active Page */ {
                    if (CPU_AL >= 0x80) {
                        uint8_t CRTCPU = RAM[BIOS_CRTCPU_PAGE];
                        switch (CPU_AL) {
                            case 0x80: /* read CRT/CPU page registers */
                                CPU_BH = CRTCPU & 7;
                                CPU_BL = (CRTCPU >> 3) & 7;
                                break;
                            case 0x81: /* set CPU page register to value in BL */
                                CRTCPU = (CRTCPU & 0xc7) | ((CPU_BL & 7) << 3);
                                break;
                            case 0x82: /* set CRT page register to value in BH */
                                CRTCPU = (CRTCPU & 0xf8) | (CPU_BH & 7);
                                break;
                            case 0x83: /* set CRT and CPU page registers in BH and BL */
                                CRTCPU = (CRTCPU & 0xc0) | (CPU_BH & 7) | ((CPU_BL & 7) << 3);
                                break;
                        }
                        tga_portout(0x3df, CRTCPU);
                        RAM[BIOS_CRTCPU_PAGE] = CRTCPU;
                        return;
                    }

                    break;
                }
                case 0x10:
                    switch (CPU_AL) {
                        case 0x00: {
                            uint8_t color_index = CPU_BL & 0xF;
                            uint8_t color_byte = CPU_BH;

                            const uint16_t r = (((color_byte >> 2) & 1) << 1) + (color_byte >> 5 & 1);
                            const uint16_t g = (((color_byte >> 1) & 1) << 1) + (color_byte >> 4 & 1);
                            const uint16_t b = (((color_byte >> 0) & 1) << 1) + (color_byte >> 3 & 1);

                            if (videomode <= 0xa) {
                                tga_palette_map[color_index] = color_byte;
                            } else {
                                vga_palette[color_index] = rgb((r * 85), (g * 85), (b * 85));
#if PICO_ON_DEVICE
                                graphics_set_palette(color_index, vga_palette[color_index]);
#endif
                            }
                            return;
                        }
                        case 0x02: {
                            uint32_t memloc = CPU_ES * 16 + CPU_DX;
                            for (int color_index = 0; color_index < 16; color_index++) {
                                uint8_t color_byte = read86(memloc++);
                                const uint8_t r = (((color_byte >> 2) & 1) << 1) + (color_byte >> 5 & 1);
                                const uint8_t g = (((color_byte >> 1) & 1) << 1) + (color_byte >> 4 & 1);
                                const uint8_t b = (((color_byte >> 0) & 1) << 1) + (color_byte >> 3 & 1);

                                vga_palette[color_index] = rgb((r * 85), (g * 85), (b * 85));
#if PICO_ON_DEVICE
                                graphics_set_palette(color_index, vga_palette[color_index]);
#endif
                            }
                            // TODO: Overscan/Border 17th color
                            return;
                        }
                        case 0x03: {
                            cga_blinking = CPU_BL ? 0x7F : 0xFF;
                            cga_blinking_lock = !CPU_BL;
                            //printf("[CPU] INT BL 0x%02x\r\n", CPU_BL);
                            return;
                        }
                        case 0x10: {
                            // Set One DAC Color Register
                            vga_palette[CPU_BL] = rgb((CPU_DH & 63) << 2, (CPU_CH & 63) << 2,
                                                      (CPU_CL & 63) << 2);
#if PICO_ON_DEVICE
                            graphics_set_palette(CPU_BL, vga_palette[CPU_BL]);
#endif
                            return;
                        }
                        case 0x12: {
                            // set block of DAC color registers               VGA
                            uint32_t memloc = CPU_ES * 16 + CPU_DX;
                            for (int color_index = CPU_BX; color_index < ((CPU_BX + CPU_CX) & 0xFF); color_index++) {
                                vga_palette[color_index] = rgb((read86(memloc++) << 2), (read86(memloc++) << 2),
                                                               (read86(memloc++) << 2));
#if PICO_ON_DEVICE
                                graphics_set_palette(color_index, vga_palette[color_index]);
#endif
                            }
                            return;
                        }
                        case 0x15: {
                            // Read One DAC Color Register
                            const uint8_t color_index = CPU_BX & 0xFF;
                            CPU_CL = ((vga_palette[color_index] >> 2)) & 63;
                            CPU_CH = ((vga_palette[color_index] >> 10)) & 63;
                            CPU_DH = ((vga_palette[color_index] >> 18)) & 63;
                            return;
                        }
                        case 0x17: {
                            // Read a Block of DAC Color Registers
                            uint32_t memloc = CPU_ES * 16 + CPU_DX;
                            for (int color_index = CPU_BX; color_index < ((CPU_BX + CPU_CX) & 0xFF); color_index++) {
                                write86(memloc++, ((vga_palette[color_index] >> 2)) & 63);
                                write86(memloc++, ((vga_palette[color_index] >> 10)) & 63);
                                write86(memloc++, ((vga_palette[color_index] >> 18)) & 63);
                            }
                            return;
                        }
                    }
                    //printf("Unhandled 10h CPU_AL: 0x%x\r\n", CPU_AL);
                    break;
                case 0x1A: //get display combination code (ps, vga/mcga)
                    CPU_AL = 0x1A;
                    if (ega_vga_enabled) {
                        CPU_BL = 0x08;
                    } else {
                        CPU_BL = 0x05; // MCGA
                    }
                    return;
            }
            break;
        }
        case 0x13:
            return diskhandler();
        case 0x15: /* XMS */
            switch (CPU_AH) {
                case 0x87: {
                    //https://github.com/neozeed/himem.sys-2.06/blob/5761f4fc182543b3964fd0d3a236d04bac7bfb50/oemsrc/himem.asm#L690
                    //                    printf("mem move?! %x %x:%x\n", CPU_CX, CPU_ES, CPU_SI);
                    CPU_AX = 0;
                    return;
                }
                    return;
                case 0x88: {
                    CPU_AX = 64;
                    return;
                }
            }
            break;
        /**/
        case 0x19:
#if PICO_ON_DEVICE
            insertdisk(0, "\\XT\\fdd0.img");
            insertdisk(1, "\\XT\\fdd1.img");
            insertdisk(128, "\\XT\\hdd.img");
            insertdisk(129, "\\XT\\hdd2.img");
#else
            insertdisk(0, "../fdd0.img");
            insertdisk(1, "../fdd1.img");
            insertdisk(128, "../hdd.img");
            insertdisk(129, "../hdd2.img");
#endif
            if (1) {
                /* PCjr reserves the top of its internal 128KB of RAM for video RAM.  * Sidecars can extend it past 128KB but it
                 * requires DOS drivers or TSRs to modify the MCB chain so that it a) marks the video memory as reserved and b)
                 * creates a new free region above the video RAM region.
                 *
                 * Therefore, only subtract 16KB if 128KB or less is configured for this machine.
                 *
                 * Note this is not speculation, it's there in the PCjr BIOS source code:
                 * [http://hackipedia.org/browse.cgi/Computer/Platform/PC%2c%20IBM%20compatible/Video/PCjr/IBM%20Personal%20Computer%20PCjr%20Hardware%20Reference%20Library%20Technical%20Reference%20%281983%2d11%29%20First%20Edition%20Revised%2epdf]
                 * ROM BIOS source code page A-16 */

                writew86(BIOS_TRUE_MEMORY_SIZE, 640 - 16);

            }
            break;
        case 0x1A: /* Timer I/O RTC */
            switch (CPU_AH) {
                case 0x02: /* 02H: Read Time from Real-Time Clock */
                    CPU_CX = 0x2259;
                    CPU_DX = 0x0001;
                    cf = 0;
                    return;
                case 0x04: /* 04H: Read Date from Real-Time Clock */
                    CPU_CX = 0x2024;
                    CPU_DX = 0x1024;
                    CPU_AH = 0;
                    cf = 0;
                    return;
            }
            break;
        case 0x2F: /* Multiplex Interrupt */
            switch (CPU_AX) {
                /* XMS */
                case 0x4300:
                    CPU_AL = 0x80;
                    return;
                case 0x4310: {
                    CPU_ES = XMS_FN_CS; // to be handled by DOS memory manager using
                    CPU_BX = XMS_FN_IP; // CALL FAR ES:BX
                    return;
                default:
                    if (redirector_handler()) {
                        return;
                    }
                }
            }
            break;
    }


	uint32_t idtentry, idtptr, gdtentry, new_esp, old_esp, old_flags, push_eip;
	uint16_t selector, idtseg, new_ss, old_ss;
	uint8_t access, gatetype, dpl, present;
	uint8_t gdtaccess, gdtdpl;
	if (!protected) { //real mode
		// cpu_int15_handler();
		//Call HLE interrupt, if one is assigned
		/*if (strcmp(usemachine, "generic_xt") == 0) if (intnum == 0x15) { //hack to get an int 15h with the generic XT BIOS
			cpu_int15_handler();
			return;
		}*/
		/*if (intnum == 0x15 && regs.wordregs[regax] == 0xE820) {
			uint32_t addr32;
			addr32 = segcache[reges] + regs.wordregs[regdi];
			printf("0xE820 EBX = %08X, ECX = %08X, buffer = %08X\n", regs.longregs[regebx], regs.longregs[regecx], addr32);
			switch (regs.longregs[regebx]) {
			case 0:
				cpu_writel(addr32, 0);
				cpu_writel(addr32 + 4, 0);
				cpu_writel(addr32 + 8, 0x9FC00);
				cpu_writel(addr32 + 12, 0);
				cpu_writew(addr32 + 16, 1);
				regs.longregs[regebx] = 0x02938475;
				break;
			case 0x02938475:
				cpu_writel(addr32, 0x9FC00);
				cpu_writel(addr32 + 4, 0);
				cpu_writel(addr32 + 8, 0x400);
				cpu_writel(addr32 + 12, 0);
				cpu_writew(addr32 + 16, 2);
				regs.longregs[regebx] = 0x91827364;
				break;
			case 0x91827364:
				cpu_writel(addr32, 0xF0000);
				cpu_writel(addr32 + 4, 0);
				cpu_writel(addr32 + 8, 0x10000);
				cpu_writel(addr32 + 12, 0);
				cpu_writew(addr32 + 16, 2);
				regs.longregs[regebx] = 0x56473829;
				break;
			case 0x56473829:
				cpu_writel(addr32, 0x100000);
				cpu_writel(addr32 + 4, 0);
				cpu_writel(addr32 + 8, 0xFF00000);
				cpu_writel(addr32 + 12, 0);
				cpu_writew(addr32 + 16, 1);
				regs.longregs[regebx] = 0;
				break;
			}
			regs.longregs[regecx] = 0x14;
			regs.longregs[regeax] = 0x534D4150;
			cf = 0;
			return;
		}*/
		if (int_callback[intnum] != NULL) {
			(*int_callback[intnum])(intnum);
			return;
		}
		// debug_log(DEBUG_DETAIL, "Int %02Xh, real mode\n", intnum);
		//Otherwise, do a real interrupt call
		pushw(makeflagsword());
		pushw(segregs[regcs]);
		pushw(ip);
		putsegreg(regcs, getmem16(0, (uint16_t)intnum * 4 + 2));
		ip = getmem16(0, (uint16_t)intnum * 4);
		ifl = 0;
		tf = 0;
		return;
	}

	/*debug_log(DEBUG_DETAIL, "Int %02Xh, protected mode (IDTR %08X, ", intnum, idtr);
	switch (source) {
	case INT_SOURCE_EXCEPTION: debug_log(DEBUG_DETAIL, "source = exception)\n"); break;
	case INT_SOURCE_SOFTWARE: debug_log(DEBUG_DETAIL, "source = software)\n"); break;
	case INT_SOURCE_HARDWARE: debug_log(DEBUG_DETAIL, "source = hardware)\n"); break;
	}*/

	if (source == INT_SOURCE_EXCEPTION) {
		/*switch (intnum) {
		case 0:	case 1:	case 2:	case 3:	case 4:	case 5:	case 6:	case 7:
		case 8:	case 9: case 10: case 11: case 12: case 13: case 14: case 16:
		case 17: case 18: case 19:
			push_eip = saveip;
			break;
		default:
			push_eip = ip;
		}*/
		push_eip = exceptionip;
		nowrite = 0;
	}
	else {
		push_eip = ip;
	}
	old_flags = makeflagsword();

	if (v86f && (source == INT_SOURCE_SOFTWARE) && (iopl < 3)) {
		exception(13, 0); //GP(0)
		return;
	}
	idtentry = idtr + ((uint32_t)intnum << 3);
	access = cpu_read(idtentry + 5);
	gatetype = access & 0xF;
	present = access >> 7;
	dpl = (access >> 5) & 3;
	selector = cpu_readw(idtentry + 2);
	idtseg = selector & 0xFFFC;
	idtptr = (uint32_t)cpu_readw(idtentry) | ((uint32_t)cpu_readw(idtentry + 6) << 16);
	//debug_log(DEBUG_DETAIL, "Index into IDT: %08X\n", (uint32_t)intnum << 3);
	if ((cpl > dpl) && (source == INT_SOURCE_SOFTWARE)) {
		exception(13, ((uint32_t)intnum << 8) | 2 | ((source == INT_SOURCE_SOFTWARE) ? 0 : 1)); //GP
		return;
	}
	if (!present) {
		exception(11, ((uint32_t)intnum << 8) | 2 | ((source == INT_SOURCE_SOFTWARE) ? 0 : 1)); //NP
		return;
	}

	gdtentry = gdtr + (idtseg & 0xFFFC);
	gdtaccess = cpu_read(gdtentry + 5);
	gdtdpl = (gdtaccess >> 5) & 3;

	switch (gatetype) {
	case 0x5: // Task Gate
		task_switch(selector, 1);
		return;
		//printf("Task gates unimplemented!\n");
		//while(1);
	case 0x6: case 0x7: //16-bit interrupt or trap gate
		if (!(gdtaccess & 4) && (gdtdpl < cpl)) { //non-conforming segment and DPL < CPL, interrupt to inner privilege
			uint32_t addval = (selector & 3) << 3;
			new_esp = cpu_readl(trbase + 4 + addval);
			new_ss = cpu_readw(trbase + 8 + addval);
			old_esp = regs.longregs[regesp];
			old_ss = segregs[regss];
			putsegreg(regss, new_ss);
			regs.longregs[regesp] = new_esp;
			pushw(old_ss);
			pushw(old_esp);
			pushw(old_flags);
			pushw(segregs[regcs]);
			pushw(push_eip); // ip);
			if ((source == INT_SOURCE_EXCEPTION) && (intnum >= 8) && (intnum <= 17)) {
				pushw(err);
			}
			if (gatetype == 0x6) ifl = 0;
			//tr = 0;
			nt = 0;
			iopl = 0;
			v86f = 0;
			tf = 0;
			//ifl = 0;
			putsegreg(regcs, selector); // | cpl);
			ip = idtptr;
			cpl = selector & 3;
			return;
		}
		if ((gdtaccess & 4) || (gdtdpl == cpl)) { //conforming segment or DPL == CPL, interrupt to same privilege level
			pushw(old_flags);
			pushw(segregs[regcs]);
			pushw(push_eip); // ip);
			if ((source == INT_SOURCE_EXCEPTION) && (intnum == 8 || intnum == 10 || intnum == 11 ||
				intnum == 12 || intnum == 13 || intnum == 14 || intnum == 17)) {
				pushw(err);
			}
			if (gatetype == 0x6) ifl = 0;
			//tr = 0;
			nt = 0;
			tf = 0;
			//ifl = 0;
			putsegreg(regcs, selector);
			ip = idtptr;
			return;
		}
		printf("Fell through 16-bit int code! What?\n");
		while(1);
		break;

	case 0xE: case 0xF: //32-bit interrupt or trap gate
		v86f = 0;
		if ((!(gdtaccess & 4) && (gdtdpl < cpl)) || (old_flags & 0x20000)) { //non-conforming segment and DPL < CPL or VM86 mode, interrupt to inner privilege
			uint32_t addval = 0; // (selector & 3) << 3;
			new_esp = cpu_readl(trbase + 4 + addval);
			new_ss = cpu_readw(trbase + 8 + addval);
			old_esp = regs.longregs[regesp];
			old_ss = segregs[regss];
			putsegreg(regss, new_ss);
			regs.longregs[regesp] = new_esp;
			if (old_flags & 0x20000) {
				pushl(segregs[reggs]);
				pushl(segregs[regfs]);
				pushl(segregs[regds]);
				pushl(segregs[reges]);
			}
			pushl(old_ss);
			pushl(old_esp);
			pushl(old_flags);
			pushl(segregs[regcs]);
			pushl(push_eip);
			if ((source == INT_SOURCE_EXCEPTION) &&
				(intnum == 8 || intnum == 10 || intnum == 11 ||
					intnum == 12 || intnum == 13 || intnum == 14 || intnum == 17)) {
				pushl(err);
			}
			if (gatetype == 0xE) ifl = 0;
			//tr = 0;
			nt = 0;
			//iopl = 0;
			v86f = 0;
			tf = 0;
			//ifl = 0;
			//selector = idtseg;
			putsegreg(regcs, selector); // | cpl);
			ip = idtptr;
			cpl = gdtdpl; // selector & 3; // idtseg & 3; //gdtdpl;
			//putsegreg(regds, 0);
			//putsegreg(reges, 0);
			//putsegreg(regfs, 0);
			//putsegreg(reggs, 0);
			return;
		}
		if ((gdtaccess & 4) || (gdtdpl == cpl)) { //conforming segment or DPL == CPL, interrupt to same privilege level
			pushl(old_flags);
			pushl(segregs[regcs]);
			pushl(push_eip); // ip);
			if ((source == INT_SOURCE_EXCEPTION) &&
				(intnum == 8 || intnum == 10 || intnum == 11 ||
					intnum == 12 || intnum == 13 || intnum == 14 || intnum == 17)) {
				pushl(err);
			}
			if (gatetype == 0xE) ifl = 0;
			//tr = 0;
			nt = 0;
			tf = 0;
			//ifl = 0;
			putsegreg(regcs, (selector & 0xFFFC) | gdtdpl); // startcpl);
			cpl = gdtdpl;
			ip = idtptr;
			return;
		}
		printf("Fell through 32-bit int code! What?\n");
		while(1);
		break;

	default:
		printf("UNRECOGNIZED GATE TYPE: 0x%X\n", gatetype);
		while(1);
	}
}

FUNC_INLINE void cpu_callf(uint16_t selector, uint32_t ip) {
	uint32_t gdtentry, new_esp, old_esp;
	uint16_t new_ss, old_ss;
	uint8_t access, dpl, flags;

	if (!protected || v86f) {
		if (isoper32) {
			pushl(segregs[regcs]);
			pushl(ip);
		}
		else {
			pushw(segregs[regcs]);
			pushw(ip);
		}
		ip = ip;
		putsegreg(regcs, selector);
		return;
	}

	old_ss = segregs[regss];
	old_esp = regs.longregs[regesp];

	if ((selector & 0xFFFC) == 0) {
		exception(13, 0); //GP(0)
		return;
	}

	gdtentry = ((selector & 4) ? ldtr : gdtr) + (selector & 0xFFFC);
	access = cpu_read(gdtentry + 5);
	dpl = (access >> 5) & 3;
	flags = cpu_read(gdtentry + 6) >> 4;

	if (access & 0x10) { // code segment
		if (access & 4) { // conforming
			if ((selector & 3) > cpl) {
				exception(13, selector & 0xFFFC);
				return;
			}
		}
		else { // non-conforming
			if ((cpl > dpl) || ((selector & 3) > dpl)) {
				// Privilege level change - inner level call
				uint32_t addval = (selector & 3) << 3;
				if (isoper32) {
					new_esp = cpu_readl(trbase + 4 + addval);
					new_ss = cpu_readw(trbase + 8 + addval);
					pushl(segregs[regss]);
					pushl(regs.longregs[regesp]);
					putsegreg(regss, new_ss);
					regs.longregs[regesp] = new_esp;
					pushl(segregs[regcs]);
					pushl(ip);
					putsegreg(regcs, selector);
					ip = ip;
					cpl = selector & 3; // dpl;
					return;
				}
				else {
					new_esp = cpu_readw(trbase + 4 + addval);
					new_ss = cpu_readw(trbase + 8 + addval);
					pushw(segregs[regss]);
					pushw(regs.wordregs[regsp]);
					putsegreg(regss, new_ss);
					regs.wordregs[regsp] = new_esp;
					pushw(segregs[regcs]);
					pushw(ip);
					putsegreg(regcs, selector);
					ip = ip;
					cpl = selector & 3; // dpl;
					return;
				}
			}
		}
		if (!(access & 0x80)) {
			exception(11, selector & 0xFFFC); //NP
			return;
		}
		if (isoper32) {
			pushl(segregs[regcs]);
			pushl(ip);
		}
		else {
			pushw(segregs[regcs]);
			pushw(ip);
		}
		putsegreg(regcs, selector);
		ip = ip;
		return;
	}

	// system segment - not supported
	exception(13, selector & 0xFFFC);
}

FUNC_INLINE void cpu_retf(uint32_t adjust) {
	uint32_t new_ip, new_esp, gdtentry;
	uint16_t new_cs, new_ss;
	uint8_t access, dpl;

	if (!protected || v86f) {
		if (isoper32) {
			ip = popl();
			putsegreg(regcs, popl());
			regs.longregs[regesp] += adjust;
		}
		else {
			ip = popw();
			putsegreg(regcs, popw());
			regs.wordregs[regsp] += adjust;
		}
		return;
	}

	if (isoper32) {
		new_ip = cpu_readl(segcache[regss] + regs.longregs[regesp]);
		new_cs = cpu_readw(segcache[regss] + regs.longregs[regesp] + 4);

		// Validate code segment
		gdtentry = ((new_cs & 4) ? ldtr : gdtr) + (new_cs & 0xFFFC);
		access = cpu_read(gdtentry + 5);
		dpl = (access >> 5) & 3;

		if (!(access & 0x10)) {
			exception(13, new_cs); // Not a code segment
			return;
		}
		if (!(access & 0x80)) {
			exception(11, new_cs); // Not present
			return;
		}

		if ((new_cs & 3) == cpl) {
			// Same privilege
			putsegreg(regcs, new_cs);
			ip = new_ip;
			regs.longregs[regesp] += 8 + adjust;
			return;
		}

		// Outer privilege level return
		if ((cpl > dpl) || ((new_cs & 3) > dpl)) {
			exception(13, new_cs); // GP
			return;
		}

		new_esp = cpu_readl(segcache[regss] + regs.longregs[regesp] + 8);
		new_ss = cpu_readw(segcache[regss] + regs.longregs[regesp] + 12);

		// Validate SS
		uint32_t ss_entry = ((new_ss & 4) ? ldtr : gdtr) + (new_ss & 0xFFFC);
		uint8_t ss_access = cpu_read(ss_entry + 5);
		if (!(ss_access & 0x10) || ((ss_access >> 3) & 3) != 1 || !(ss_access & 0x80)) {
			exception(11, new_ss); // Not present or not writable data segment
			return;
		}

		putsegreg(regss, new_ss);
		regs.longregs[regesp] = new_esp + adjust;
		putsegreg(regcs, new_cs);
		ip = new_ip;
		cpl = new_cs & 3;
	}
	else {
		new_ip = cpu_readw(segcache[regss] + (uint32_t)regs.wordregs[regsp]);
		new_cs = cpu_readw(segcache[regss] + (uint32_t)regs.wordregs[regsp] + 2);

		gdtentry = ((new_cs & 4) ? ldtr : gdtr) + (new_cs & 0xFFFC);
		access = cpu_read(gdtentry + 5);
		dpl = (access >> 5) & 3;

		if (!(access & 0x10)) {
			exception(13, new_cs);
			return;
		}
		if (!(access & 0x80)) {
			exception(11, new_cs);
			return;
		}

		if ((new_cs & 3) == cpl) {
			putsegreg(regcs, new_cs);
			ip = new_ip;
			regs.wordregs[regsp] += 4 + adjust;
			return;
		}

		if ((cpl > dpl) || ((new_cs & 3) > dpl)) {
			exception(13, new_cs);
			return;
		}

		new_esp = cpu_readw(segcache[regss] + (uint32_t)regs.wordregs[regsp] + 4);
		new_ss = cpu_readw(segcache[regss] + (uint32_t)regs.wordregs[regsp] + 6);

		uint32_t ss_entry = ((new_ss & 4) ? ldtr : gdtr) + (new_ss & 0xFFFC);
		uint8_t ss_access = cpu_read(ss_entry + 5);
		if (!(ss_access & 0x10) || ((ss_access >> 3) & 3) != 1 || !(ss_access & 0x80)) {
			exception(11, new_ss);
			return;
		}

		putsegreg(regss, new_ss);
		regs.wordregs[regsp] = new_esp + adjust;
		putsegreg(regcs, new_cs);
		ip = new_ip;
		cpl = new_cs & 3;
	}
}

FUNC_INLINE void cpu_iret() {
	uint32_t old_esp, new_esp, new_cs, new_eip, new_eflags, new_ss, new_es, new_ds, new_fs, new_gs;
	uint32_t gdtentry;
	uint8_t gdtaccess, gdtdpl;

	if (!protected) { //real mode
		if (isoper32) {
			new_eip = popl();
			new_cs = popl();
			new_eflags = popl();
			putsegreg(regcs, new_cs);
			ip = new_eip;
			decodeflagsword(new_eflags);
			return;
		}
		new_eip = popw();
		new_cs = popw();
		new_eflags = (uint32_t)popw() | (makeflagsword() & 0xFFFF0000);
		putsegreg(regcs, new_cs);
		ip = new_eip;
		decodeflagsword(new_eflags);
		return;
	}

	//protected mode
	if (v86f) {
		if (iopl < 3) {
			exception(13, 0); //GP(0)
			return;
		}
		if (isoper32) {
			new_eip = cpu_readl(segcache[regss] + (regs.wordregs[regsp] & 0xFFFF));
			new_cs = cpu_readl(segcache[regss] + ((regs.wordregs[regsp] + 4) & 0xFFFF));
			new_eflags = cpu_readl(segcache[regss] + ((regs.wordregs[regsp] + 8) & 0xFFFF));
			gdtentry = gdtr + (new_cs & 0xFFFC);
			gdtaccess = cpu_read(gdtentry + 5);
			gdtdpl = (gdtaccess >> 5) & 3;
			putsegreg(regcs, new_cs);
			cpl = gdtdpl;
			ip = new_eip;
			regs.wordregs[regsp] += 12;
			decodeflagsword(new_eflags);
			return;
		}
		new_eip = cpu_readw(segcache[regss] + (regs.wordregs[regsp] & 0xFFFF));
		new_cs = cpu_readw(segcache[regss] + ((regs.wordregs[regsp] + 2) & 0xFFFF));
		new_eflags = (uint32_t)cpu_readw(segcache[regss] + ((regs.wordregs[regsp] + 4) & 0xFFFF)) | (makeflagsword() & 0xFFFF0000);
		putsegreg(regcs, new_cs);
		ip = new_eip;
		regs.wordregs[regsp] += 6;
		decodeflagsword(new_eflags);
		return;
	}

	if (nt) {
		uint16_t backlink = cpu_readw(trbase + 0x00);
		task_switch(backlink, 0);
		nt = 0;
		return;
	}

	if (isoper32) {
		new_eip = cpu_readl(segcache[regss] + regs.longregs[regesp]);
		new_cs = cpu_readw(segcache[regss] + regs.longregs[regesp] + 4);
		new_eflags = cpu_readl(segcache[regss] + regs.longregs[regesp] + 8);

		if (v86f) {
			new_eflags = (new_eflags & 0xFFFF) | (makeflagsword() & 0xFFFF0000);
		}

		if (new_eflags & 0x20000) { //if V86 flag set in new flags, we're returning to V86 mode
			new_esp = cpu_readl(segcache[regss] + regs.longregs[regesp] + 12);
			new_ss = cpu_readl(segcache[regss] + regs.longregs[regesp] + 16);
			new_es = cpu_readl(segcache[regss] + regs.longregs[regesp] + 20);
			new_ds = cpu_readl(segcache[regss] + regs.longregs[regesp] + 24);
			new_fs = cpu_readl(segcache[regss] + regs.longregs[regesp] + 28);
			new_gs = cpu_readl(segcache[regss] + regs.longregs[regesp] + 32);
			decodeflagsword(new_eflags);
			putsegreg(regss, new_ss);
			regs.longregs[regesp] = new_esp;
			putsegreg(reges, new_es);
			putsegreg(regds, new_ds);
			putsegreg(regfs, new_fs);
			putsegreg(reggs, new_gs);
			putsegreg(regcs, new_cs);
			ip = new_eip & 0xFFFF;
			cpl = 3;
			return;
		}
	}
	else { //16-bit mode
		new_eip = cpu_readw(segcache[regss] + (regs.wordregs[regsp] & 0xFFFF));
		new_cs = cpu_readw(segcache[regss] + ((regs.wordregs[regsp] + 2) & 0xFFFF));
		new_eflags = cpu_readw(segcache[regss] + ((regs.wordregs[regsp] + 4) & 0xFFFF)) | (makeflagsword() & 0xFFFF0000);
	}

	gdtentry = gdtr + (new_cs & 0xFFFC);
	gdtaccess = cpu_read(gdtentry + 5);
	gdtdpl = (gdtaccess >> 5) & 3;

	if (new_cs == 0) {
		exception(13, 0); //GP(0)
		return;
	}
	if ((new_cs & 3) != gdtdpl) {
		exception(13, new_cs);
		return;
	}
	if ((new_cs & 3) == cpl) { //return same level
		putsegreg(regcs, new_cs);
		cpl = new_cs & 3; // gdtdpl;
		ip = new_eip;
		if ((new_cs & 3) == 3) {
			new_eflags = (makeflagsword() & 0x00037200) | (new_eflags & ~0x00037200);
		}
		decodeflagsword(new_eflags);
		regs.longregs[regesp] += isoper32 ? 12 : 6;
		return;
	}

	if ((new_cs & 3) > cpl) {
		//return outer level
		if (isoper32) { //32-bit
			new_esp = cpu_readl(segcache[regss] + regs.longregs[regesp] + 12);
			new_ss = cpu_readw(segcache[regss] + regs.longregs[regesp] + 16);
		}
		else { //16-bit
			new_esp = cpu_readw(segcache[regss] + ((regs.longregs[regesp] + 6) & 0xFFFF));
			new_ss = cpu_readw(segcache[regss] + ((regs.longregs[regesp] + 8) & 0xFFFF));
		}
		putsegreg(regcs, new_cs);
		ip = new_eip;
		//if (cpl > 0) {
		/*if ((new_cs & 3) == 3) {
			new_eflags = (makeflagsword() & 0x00037200) | (new_eflags & ~0x00037200);
		}*/
		decodeflagsword(new_eflags);
		putsegreg(regss, new_ss);
		regs.longregs[regesp] = new_esp;
		cpl = new_cs & 3;

		//putsegreg(reges, 0);
		//putsegreg(regfs, 0);
		//putsegreg(reggs, 0);
		//putsegreg(regds, 0);
		return;
	}

	printf("Fell through IRET code!!\n");
}
/*
int cpu_interruptCheck(int slave) {
	//if (protected) return;
	/* get next interrupt from the i8259, if any * /
	if (!trap_toggle && (ifl && (i8259->irr & (~i8259->imr)))) {
		//if (i8259->irr & (~i8259->imr) & (~i8259->isr)) {
		uint8_t intnum = i8259_nextintr(i8259) + i8259->intoffset;
		//if (protected) intnum += 0x20; else intnum += 8;
		//printf("Intnum: %02X\n", intnum);
		hltstate = 0;
		cpu_intcall(intnum, INT_SOURCE_HARDWARE, 0);
		return 1;
	}
	return 0;
}
*/
uint32_t firstip;
int showops = 0;


void op_ext_00() {
	modregrm();
	//debug_log(DEBUG_DETAIL, "0F 00 reg %u", reg);
	//printf("0F 00 reg %u", reg);
	switch (reg) {
	case 0: //SLDT
		writerm16(rm, ldt_selector);
		break;
	case 1: //STR
		writerm16(rm, tr_selector);
		break;
	case 2: //LLDT
		temp16 = readrm16(rm);
		ldt_selector = temp16;
		tempaddr32 = gdtr + (uint32_t)(temp16 & ~7);
		ldtl = (uint32_t)cpu_readw(tempaddr32) | ((uint32_t)(cpu_read(tempaddr32 + 6) & 0xF) << 16);
		if (cpu_read(tempaddr32 + 6) & 0x80) {
			ldtl <<= 12;
			ldtl |= 0xFFF;
		}
		ldtr = cpu_readw(tempaddr32 + 2) | ((uint32_t)cpu_read(tempaddr32 + 4) << 16) | (((uint32_t)cpu_read(tempaddr32 + 7) >> 4) << 24);
		//debug_log(DEBUG_DETAIL, "Loaded LDT from %08X (LDT selector %04X (%04X), location is %08X, limit %u)", tempaddr32, ldt_selector, ldt_selector>>3, ldtr, ldtl);
		break;
	case 3: //LTR
	{
		uint8_t access_byte, dpl, present, type, cpl;
		temp16 = readrm16(rm);
		tr_selector = temp16;
		tempaddr32 = gdtr + ((temp16 >> 3) * 8);
		access_byte = cpu_read(tempaddr32 + 5);
		type = access_byte & 0x0F;
		dpl = (access_byte >> 5) & 0x03;
		cpl = segregs[regcs] & 3;
		present = (access_byte >> 7) & 0x01;

		if ((type != 0x9) && (type != 0xB) && (type != 0x3) && (type != 0x7)) {
			//cpu_exception(13, temp16); //GP
			//return;
		}
		else if (!present) {
			//cpu_intcall(11, INT_SOURCE_EXCEPTION, temp16); //NP
			exception(11, temp16); //NP
			return;
		}
		else if (cpl > dpl) {
			//debug_log(DEBUG_DETAIL, "cpl > dpl check failed");
			exception(13, temp16); //GP
			return;
		}
		//TODO: TSS busy check
		else {
			uint8_t new_type;
			trtype = type;
			trlimit = cpu_readw(tempaddr32) + 1;
			trbase = (uint32_t)cpu_read(tempaddr32 + 2) |
				((uint32_t)cpu_read(tempaddr32 + 3) << 8) |
				((uint32_t)cpu_read(tempaddr32 + 4) << 16) |
				((uint32_t)cpu_read(tempaddr32 + 7) << 24);
			new_type = (type == 0x9) ? 0xB : 0x7;
			cpu_write(tempaddr32 + 5, (access_byte & 0xF0) | new_type);
			//debug_log(DEBUG_DETAIL, "Loaded TR from %08X (TR location is %08X, limit %u)", tempaddr32, trbase, trlimit);
		}
		break;
	}
	case 4: //VERR
		//while(1);
		printf("VERR");
		zf = 1;
		break;
	case 5: //VERW
		//while(1);
		printf("VERW");
		zf = 1;
		break;
	default:
		//cpu_intcall(6, INT_SOURCE_EXCEPTION, 0);
		exception(6, 0); //UD
	}
}


void op_ext_01() {
	modregrm();
	//debug_log(DEBUG_DETAIL, "0F 01 reg %u", reg);
	//printf("0F 01 reg %u", reg);
	switch (reg) {
	case 0: //SGDT
		getea(rm);
		cpu_writew(ea, gdtl);
		if (isoper32) {
			cpu_writel(ea + 2, gdtr);
		}
		else {
			cpu_writew(ea + 2, (uint16_t)gdtr);
			cpu_write(ea + 4, (uint8_t)(gdtr >> 16));
			//cpu_write(ea + 5, (uint8_t)(gdtr >> 24));
			//cpu_write(ea + 5, 0);
		}
		break;
	case 1: //SIDT
		getea(rm);
		cpu_writew(ea, idtl);
		if (isoper32) {
			cpu_writel(ea + 2, idtr);
		}
		else {
			cpu_writew(ea + 2, (uint16_t)idtr);
			cpu_write(ea + 4, (uint8_t)(idtr >> 16));
			//cpu_write(ea + 5, (uint8_t)(idtr >> 24));
			//cpu_write(ea + 5, 0);
		}
		break;
	case 2: //LGDT
		if (protected && (cpl > 0)) {
			debug_log(DEBUG_INFO, "Attempted to use LGDT when CPU was already in protected mode and CPL>0!");
			exception(13, 0); //GP
			break;
		}
		getea(rm);
		gdtl = cpu_readw(ea);
		if (isoper32) {
			gdtr = cpu_readl(ea + 2);
		}
		else {
			gdtr = cpu_readw(ea + 2) | ((uint32_t)cpu_read(ea + 4) << 16);
		}
		//debug_log(DEBUG_DETAIL, "Loaded GDT from %08X (GDT location is %08X, limit %u)", ea, gdtr, gdtl);
		break;
	case 3: //LIDT
		getea(rm);
		idtl = cpu_readw(ea);
		if (isoper32) {
			idtr = cpu_readl(ea + 2);
		}
		else {
			idtr = cpu_readw(ea + 2) | ((uint32_t)cpu_read(ea + 4) << 16);
		}
		//showops = 1;
		//debug_log(DEBUG_DETAIL, "Loaded IDT from %08X (IDT location is %08X, limit %u)", ea, idtr, idtl);
		break;
	case 4: //SMSW
		writerm16(rm, ((uint16_t)cr[0] | (have387 ? 0 : 4))); //still use 32-bit write, upper bits are zeros in destination
		break;
	case 6: //LMSW
	{
		uint32_t oldpm = cr[0] & 1;
		cr[0] = (cr[0] & 0xFFFFFFE1) | (readrm16(rm) & 0x1E);
		cr[0] |= readrm16(rm) & 0x11;
		if ((cr[0] & 1) && !oldpm) {
			protected = 1;
			ifl = 0;
			//debug_log(DEBUG_DETAIL, "Entered protected mode");
		}

		break;
	}
	case 7: //INVLPG
		//printf("INVLPG");
		//while(1);
		break;
	}
}

//LAR
void op_ext_02() {
	uint32_t gdtidx, lar;
	uint8_t access, flags;
	//TODO: validate selector/segment + return software available bit
	modregrm();
	oper1 = readrm16(rm);
	gdtidx = ((oper1 & 4) ? ldtr : gdtr) + ((uint32_t)(oper1 >> 3) * 8);
	access = cpu_read(gdtidx + 5);
	flags = cpu_read(gdtidx + 6) >> 5;
	lar = (uint32_t)access << 8;
	lar |= (uint32_t)flags << 21;
	if (isoper32) {
		putreg32(reg, lar);
	}
	else {
		putreg16(reg, (uint16_t)lar);
	}
	zf = 1;
}

//LSL
void op_ext_03() {
	uint32_t gdtidx, limit;
	//TODO: validate selector/segment
	modregrm();
	oper1 = readrm16(rm);
	gdtidx = ((oper1 & 4) ? ldtr : gdtr) + ((uint32_t)(oper1 >> 3) * 8);
	limit = (uint32_t)cpu_readw(gdtidx) | ((uint32_t)(cpu_read(gdtidx + 6) & 0xF) << 16);
	if (cpu_read(gdtidx + 6) & 0x80) {
		limit <<= 12;
		limit |= 0xFFF;
	}
	//printf("LSL for %04X = %lu", oper1, limit);
	if (isoper32) {
		putreg32(reg, limit);
	}
	else {
		putreg16(reg, (uint16_t)limit);
	}
	zf = 1;
}

//CLTS
void op_ext_06() {
	if (cpl > 0) {
		exception(13, 0); //GP(0)
		return;
	}
	cr[0] &= ~8;
}

//INVD
//WBINVD
void op_ext_08_09() {

}

//MOV r32, CRn
void op_ext_20() {
	//TODO: exceptions on invalid CRn and other stuff
	modregrm();
	if (cpl > 0) {
		exception(13, 0); //GP
		return;
	}
	regs.longregs[rm] = cr[reg];
	//if (reg == 0) regs.longregs[rm] |= (have387 ? 0 : 4); //TODO: use this?
}

//MOV CRn, r32
void op_ext_22() {
	modregrm();
	if (cpl > 0) {
		exception(13, 0); //GP
		return;
	}
	//debug_log(DEBUG_DETAIL, "CR%u <- %08X", reg, regs.longregs[rm]);
	cr[reg] = regs.longregs[rm];
	//cr[0] &= 0x00000037; //mask out bits 29 (NW) and 30 (CD) which are not present on 386
	switch (reg) {
	case 0: //CR0
		if (cr[0] & 1) {
			protected = 1;
			ifl = 0;
			//debug_log(DEBUG_DETAIL, "Entered protected mode");
			paging = (cr[0] & 0x80000000) ? 1 : 0;
		}
		else {
			protected = 0;
			paging = 0;
			usegdt = 0;
			isoper32 = 0;
			isaddr32 = 0;
			isCS32 = 0;
			memset(segis32, 0, sizeof(segis32));
			//debug_log(DEBUG_DETAIL, "Entered real mode");
		}
		break;
	case 4: //CR4
		printf("CR4: %08X", cr[4]);
		break;
	}
}

//MOV r32, DBn
void op_ext_21() {
	//TODO: stuff
	modregrm();
	regs.longregs[rm] = dr[reg];
}

//MOV DBn, r32
void op_ext_23() {
	modregrm();
	dr[reg] = regs.longregs[rm];
}


void op_ext_24_26() {
	modregrm();
}

//WRMSR
void op_ext_30() {

}

//RDTSC (not accurate, it should really be clock cycles)
void op_ext_31() {
	regs.longregs[regedx] = totalexec >> 32;
	regs.longregs[regeax] = (uint32_t)totalexec;
}

//CMOVO
void op_ext_40() {
	modregrm();
	if (of) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNO
void op_ext_41() {
	modregrm();
	if (!of) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVB
void op_ext_42() {
	modregrm();
	if (cf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNB
void op_ext_43() {
	modregrm();
	if (!cf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVZ
void op_ext_44() {
	modregrm();
	if (zf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNZ
void op_ext_45() {
	modregrm();
	if (!zf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVBE
void op_ext_46() {
	modregrm();
	if (cf || zf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNBE
void op_ext_47() {
	modregrm();
	if (!cf && !zf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVS
void op_ext_48() {
	modregrm();
	if (sf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNS
void op_ext_49() {
	modregrm();
	if (!sf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVP
void op_ext_4A() {
	modregrm();
	if (pf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNP
void op_ext_4B() {
	modregrm();
	if (!pf) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVL
void op_ext_4C() {
	modregrm();
	if (sf != of) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNL
void op_ext_4D() {
	modregrm();
	if (sf == of) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVLE
void op_ext_4E() {
	modregrm();
	if (zf || (sf != of)) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

//CMOVNLE
void op_ext_4F() {
	modregrm();
	if (!zf && (sf == of)) {
		if (isoper32) {
			putreg32(reg, readrm32(rm));
		}
		else {
			putreg16(reg, readrm16(rm));
		}
	}
}

// 80 JO Jb
void op_ext_80() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (of) {
		ip = ip + temp32;
	}
}

// 81 JNO Jb
void op_ext_81() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!of) {
		ip = ip + temp32;
	}
}

// 82 JB Jb
void op_ext_82() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (cf) {
		ip = ip + temp32;
	}
}

// 83 JNB Jb
void op_ext_83() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!cf) {
		ip = ip + temp32;
	}
}

// 84 JZ Jb
void op_ext_84() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (zf) {
		ip = ip + temp32;
	}
}

// 85 JNZ Jb
void op_ext_85() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!zf) {
		ip = ip + temp32;
	}
}

// 86 JBE Jb
void op_ext_86() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (cf || zf) {
		ip = ip + temp32;
	}
}

// 87 JA Jb
void op_ext_87() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!cf && !zf) {
		ip = ip + temp32;
	}
}

// 88 JS Jb
void op_ext_88() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (sf) {
		ip = ip + temp32;
	}
}

// 89 JNS Jb
void op_ext_89() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!sf) {
		ip = ip + temp32;
	}
}

// 8A JPE Jb
void op_ext_8A() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (pf) {
		ip = ip + temp32;
	}
}

// 8B JPO Jb
void op_ext_8B() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!pf) {
		ip = ip + temp32;
	}
}

// 8C JL Jb
void op_ext_8C() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (sf != of) {
		ip = ip + temp32;
	}
}

// 8D JGE Jb
void op_ext_8D() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (sf == of) {
		ip = ip + temp32;
	}
}

// 8E JLE Jb
void op_ext_8E() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if ((sf != of) || zf) {
		ip = ip + temp32;
	}
}

// 8F JG Jb
void op_ext_8F() {
	if (isoper32) {
		temp32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		temp32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (!zf && (sf == of)) {
		ip = ip + temp32;
	}
}

// 90 SETO Jb
void op_ext_90() {
	modregrm();
	writerm8(rm, (of) ? 1 : 0);
}

// 91 SETNO Jb
void op_ext_91() {
	modregrm();
	writerm8(rm, (!of) ? 1 : 0);
}

// 92 SETB Jb
void op_ext_92() {
	modregrm();
	writerm8(rm, (cf) ? 1 : 0);
}

// 93 SETNB Jb
void op_ext_93() {
	modregrm();
	writerm8(rm, (!cf) ? 1 : 0);
}

// 94 SETZ Jb
void op_ext_94() {
	modregrm();
	writerm8(rm, (zf) ? 1 : 0);
}

// 95 SETNZ Jb
void op_ext_95() {
	modregrm();
	writerm8(rm, (!zf) ? 1 : 0);
}

// 96 SETBE Jb
void op_ext_96() {
	modregrm();
	writerm8(rm, (cf || zf) ? 1 : 0);
}

// 97 SETA Jb
void op_ext_97() {
	modregrm();
	writerm8(rm, (!cf && !zf) ? 1 : 0);
}

// 98 SETS Jb
void op_ext_98() {
	modregrm();
	writerm8(rm, (sf) ? 1 : 0);
}

// 99 SETNS Jb
void op_ext_99() {
	modregrm();
	writerm8(rm, (!sf) ? 1 : 0);
}

// 9A SETPE Jb
void op_ext_9A() {
	modregrm();
	writerm8(rm, (pf) ? 1 : 0);
}

// 9B SETPO Jb
void op_ext_9B() {
	modregrm();
	writerm8(rm, (!pf) ? 1 : 0);
}

// 9C SETL Jb
void op_ext_9C() {
	modregrm();
	writerm8(rm, (sf != of) ? 1 : 0);
}

// 9D SETGE Jb
void op_ext_9D() {
	modregrm();
	writerm8(rm, (sf == of) ? 1 : 0);
}

// 9E SETLE Jb
void op_ext_9E() {
	modregrm();
	writerm8(rm, ((sf != of) || zf) ? 1 : 0);
}

// 9F SETG Jb
void op_ext_9F() {
	modregrm();
	writerm8(rm, (!zf && (sf == of)) ? 1 : 0);
}

//PUSH FS
void op_ext_A0() {
	push(getsegreg(regfs));
}

//POP FS
void op_ext_A1() {
	putsegreg(regfs, pop());
}

//CPUID
void op_ext_A2() {
	switch (regs.longregs[regeax]) {
	case 0:
		regs.longregs[regeax] = 1;
		regs.longregs[regebx] = 'G' | ('e' << 8) | ('n' << 16) | ('u' << 24);
		regs.longregs[regedx] = 'i' | ('n' << 8) | ('e' << 16) | ('I' << 24);
		regs.longregs[regecx] = 'n' | ('t' << 8) | ('e' << 16) | ('l' << 24);
		break;
	case 1:
		regs.longregs[regeax] =
			(0 << 0)  // Stepping
			| (0 << 4)  // Model
			| (4 << 8); // Family (i486)
		regs.longregs[regebx] = regs.longregs[regecx] = 0;
		regs.longregs[regedx] =
			((have387 ? 1 : 0) << 0) | // FPU
			(1 << 4) | // TSC
			(0 << 8) | // CMPXCHG8B
			(1 << 15) | // CMOV
			(0 << 23) | // MMX
			(0 << 24) | // FXSR
			(0 << 25) | // SSE
			(0 << 26);  // SSE2
		break;
	default:
		regs.longregs[regeax] = regs.longregs[regebx] = regs.longregs[regecx] = regs.longregs[regedx] = 0;
	}
}

//BT reg
void op_ext_A3() {
	modregrm();
	if (isoper32) {
		oper2_32 = getreg32(reg);
		if (mode == 3) {
			oper1_32 = readrm32(rm);
		}
		else { //BT can read memory beyond 32 bits
			if (oper2_32 & 0x80000000) printf("negative 0xA3 32-bit");
			getea(rm);
			ea += (oper2_32 / 32) * 4;
			oper1_32 = cpu_readl(ea);
		}
		cf = (oper1_32 >> (oper2_32 & 31)) & 1;
	}
	else {
		oper2 = getreg16(reg);
		if (mode == 3) {
			oper1 = readrm16(rm);
		}
		else {
			if (oper2 & 0x8000) printf("negative 0xA3 16-bit");
			getea(rm);
			ea += (oper2 / 16) * 2;
			oper1 = cpu_readw(ea);
		}
		cf = (oper1 >> (oper2 & 15)) & 1;
	}
}

//SHLD r/m, r, imm8
//SHLD r/m, r, CL
void op_ext_A4_A5() {
	uint8_t count;
	modregrm();
	if (opcode == 0xA4) {
		count = getmem8(segcache[regcs], ip);
		StepIP(1);
	}
	else {
		count = regs.byteregs[regcl];
	}

	if (isoper32) {
		uint32_t dest = readrm32(rm);
		uint32_t src = getreg32(reg);
		count &= 0x1F;

		if (count != 0) {
			cf = (dest >> (32 - count)) & 1;
			res32 = (dest << count) | (src >> (32 - count));
			writerm32(rm, res32);
			flag_szp32(res32);
			if (count == 1)
				of = (((res32 >> 31) & 1) ^ cf);
			else
				of = 0;
		}
	}
	else {
		uint16_t dest = readrm16(rm);
		uint16_t src = getreg16(reg);
		count &= 0x1F;

		if (count != 0) {
			cf = (dest >> (16 - count)) & 1;
			res16 = (dest << count) | (src >> (16 - count));
			writerm16(rm, res16);
			flag_szp16(res16);
			if (count == 1)
				of = (((res16 >> 15) & 1) ^ cf);
			else
				of = 0;
		}
	}
}

//CMPXCHG
void op_ext_A6_B0() {
	modregrm();
	oper1b = regs.byteregs[regal]; // readreg(reg);
	oper2b = readrm8(rm);
	op_sub8();
	if (zf) {
		writerm8(rm, getreg8(reg));
	}
	else {
		regs.byteregs[regal] = oper2b;
	}
}

//PUSH GS
void op_ext_A8() {
	push(getsegreg(reggs));
}

//POP GS
void op_ext_A9() {
	putsegreg(reggs, pop());
}

//RSM (?)
void op_ext_AA() {

}

//BTS rm, reg
void op_ext_AB() {
	modregrm();
	if (isoper32) {
		oper2_32 = getreg32(reg);
		if (mode == 3) {
			oper1_32 = readrm32(rm);
			writerm32(rm, oper1_32 | (1 << (oper2_32 & 31)));
		}
		else { //can read memory beyond 32 bits
			if (oper2_32 & 0x80000000) printf("negative 0xAB 32-bit");
			getea(rm);
			ea += (oper2_32 / 32) * 4;
			oper1_32 = cpu_readl(ea);
			cpu_writel(ea, oper1_32 | (1 << (oper2_32 & 31)));
		}
		cf = (oper1_32 >> (oper2_32 & 31)) & 1;
	}
	else {
		oper2 = getreg16(reg);
		if (mode == 3) {
			oper1 = readrm16(rm);
			writerm16(rm, oper1 | (1 << (oper2 & 15)));
		}
		else {
			if (oper2 & 0x8000) printf("negative 0xAB 16-bit");
			getea(rm);
			ea += (oper2 / 16) * 2;
			oper1 = cpu_readw(ea);
			cpu_writew(ea, oper1 | (1 << (oper2 & 15)));
		}
		cf = (oper1 >> (oper2 & 15)) & 1;
	}
}

//SHRD
void op_ext_AC_AD() {
	uint32_t count, sign;
	uint64_t temp;
	modregrm();
	if (opcode == 0xAD) {
		count = regs.byteregs[regcl];
	}
	else {
		count = getmem8(segcache[regcs], ip);
		StepIP(1);
	}
	if (count != 0) {
		if (isoper32) {
			count &= 31;
			oper1_32 = readrm32(rm);
			oper2_32 = getreg32(reg);
			sign = oper1_32 & 0x80000000;
			temp = oper1_32 >> count;
			temp |= ((uint64_t)oper2_32 << (32 - count));
			temp &= 0xFFFFFFFF;
			if (count > 0) cf = (oper1_32 >> (count - 1)) & 1;
			if (count == 1) of = ((temp & 0x80000000) != sign) ? 1 : 0;
			writerm32(rm, (uint32_t)temp);
		}
		else {
			count &= 15;
			oper1 = readrm16(rm);
			oper2 = getreg16(reg);
			sign = oper1 & 0x8000;
			temp = oper1 >> count;
			temp |= ((uint64_t)oper2 << (16 - count));
			temp &= 0xFFFF;
			if (count > 0) cf = (oper1 >> (count - 1)) & 1;
			if (count == 1) of = ((temp & 0x8000) != sign) ? 1 : 0;
			writerm16(rm, (uint16_t)temp);
		}
	}
}

//IMUL - TODO: is this right?
void op_ext_AF() {
	modregrm();
	int32_t src, dst;
	int64_t result;
	src = isoper32 ? readrm32(rm) : (int32_t)(int16_t)readrm16(rm);
	dst = isoper32 ? getreg32(reg) : (int32_t)(int16_t)getreg16(reg);
	result = (int64_t)dst * (int64_t)src;
	if (isoper32) {
		putreg32(reg, (uint32_t)result);
		if ((result & 0x80000000) && ((result & 0xFFFFFFFF00000000UL) != 0xFFFFFFFF00000000UL) ||
			!(result & 0x80000000) && ((result & 0xFFFFFFFF00000000UL) != 0UL))
			cf = of = 1;
		else
			cf = of = 0;
	}
	else {
		putreg16(reg, (uint16_t)result);
		if (((result & 0x8000) && ((result & 0xFFFF0000) != 0xFFFF0000UL)) ||
			(!(result & 0x8000) && ((result & 0xFFFF0000) != 0)))
			cf = of = 1;
		else
			cf = of = 0;
	}
}


void op_ext_B1() {
	modregrm();
	if (isoper32) {
		oper1_32 = regs.longregs[regeax]; // readreg(reg);
		oper2_32 = readrm32(rm);
		op_sub32();
		if (zf) {
			writerm32(rm, getreg32(reg));
		}
		else {
			regs.longregs[regeax] = oper2_32;
		}
	}
	else {
		oper1 = regs.wordregs[regax]; // readreg(reg);
		oper2 = readrm16(rm);
		op_sub16();
		if (zf) {
			writerm16(rm, getreg16(reg));
		}
		else {
			regs.wordregs[regax] = oper2;
		}
	}
}

//BTR rm16/32, r16/32
void op_ext_B3() {
	modregrm();
	if (isoper32) {
		oper2_32 = getreg32(reg);
		if (mode == 3) {
			oper1_32 = readrm32(rm);
			writerm32(rm, oper1_32 & ~(1 << (oper2_32 & 31)));
		}
		else { //can read memory beyond 32 bits
			if (oper2_32 & 0x80000000) printf("negative 0xB3 32-bit");
			getea(rm);
			ea += (oper2_32 / 32) * 4;
			oper1_32 = cpu_readl(ea);
			cpu_writel(ea, oper1_32 & ~(1 << (oper2_32 & 31)));
		}
		cf = (oper1_32 >> (oper2_32 & 31)) & 1;
	}
	else {
		oper2 = getreg16(reg);
		if (mode == 3) {
			oper1 = readrm16(rm);
			writerm16(rm, oper1 & ~(1 << (oper2 & 15)));
		}
		else {
			if (oper2 & 0x8000) printf("negative 0xB3 16-bit");
			getea(rm);
			ea += (oper2 / 16) * 2;
			oper1 = cpu_readw(ea);
			cpu_writew(ea, oper1 & ~(1 << (oper2 & 15)));
		}
		cf = (oper1 >> (oper2 & 15)) & 1;
	}
}

//LSS
//LFS
//LGS
void op_ext_B2_B4_B5() {
	modregrm();
	getea(rm);
	if (isoper32) {
		putreg32(reg, cpu_readl(ea));
		switch (opcode) {
		case 0xB2: putsegreg(regss, cpu_readw(ea + 4)); break;
		case 0xB4: putsegreg(regfs, cpu_readw(ea + 4)); break;
		case 0xB5: putsegreg(reggs, cpu_readw(ea + 4)); break;
		}
	}
	else {
		putreg16(reg, cpu_readw(ea));
		switch (opcode) {
		case 0xB2: putsegreg(regss, cpu_readw(ea + 2)); break;
		case 0xB4: putsegreg(regfs, cpu_readw(ea + 2)); break;
		case 0xB5: putsegreg(reggs, cpu_readw(ea + 2)); break;
		}
	}
}

//MOVZX r32/16, rm8 TODO: is this right?
void op_ext_B6() {
	modregrm();
	if (isoper32) {
		putreg32(reg, (uint32_t)readrm8(rm));
	}
	else {
		putreg16(reg, (uint16_t)readrm8(rm));
	}
}

//MOVZX r32, rm16 TODO: is this right?
void op_ext_B7() {
	modregrm();
	putreg32(reg, (uint32_t)readrm16(rm));
}

//bit operations
void op_ext_BA() {
	modregrm();
	if (isoper32) {
		oper2_32 = getmem8(segcache[regcs], ip);
		if (mode == 3) {
			oper1_32 = readrm32(rm);
		}
		else {
			getea(rm);
			ea += (oper2_32 / 32) * 4;
			oper1_32 = cpu_readl(ea);
		}
	}
	else {
		oper2 = getmem8(segcache[regcs], ip);
		if (mode == 3) {
			oper1 = readrm16(rm);
		}
		else {
			getea(rm);
			ea += (oper2 / 16) * 2;
			oper1 = cpu_readw(ea);
		}
	}
	StepIP(1);
	switch (reg) {
	case 4: //BT rm16/32, imm8
		if (isoper32) {
			cf = (oper1_32 >> (oper2_32 & 31)) & 1;
		}
		else {
			cf = (oper1 >> (oper2 & 15)) & 1;
		}
		break;
	case 5: //BTS rm16/32, imm8
		if (isoper32) {
			cf = (oper1_32 >> (oper2_32 & 31)) & 1;
			if (mode == 3) {
				writerm32(rm, oper1_32 | (1 << (oper2_32 & 31)));
			}
			else {
				cpu_writel(ea, oper1_32 | (1 << (oper2_32 & 31)));
			}
		}
		else {
			cf = (oper1 >> (oper2 & 15)) & 1;
			if (mode == 3) {
				writerm16(rm, oper1 | (1 << (oper2 & 15)));
			}
			else {
				cpu_writew(ea, oper1 | (1 << (oper2 & 15)));
			}
		}
		break;
	case 6: //BTR rm16/32, imm8
		if (isoper32) {
			cf = (oper1_32 >> (oper2_32 & 31)) & 1;
			if (mode == 3) {
				writerm32(rm, oper1_32 & ~(1 << (oper2_32 & 31)));
			}
			else {
				cpu_writel(ea, oper1_32 & ~(1 << (oper2_32 & 31)));
			}
		}
		else {
			cf = (oper1 >> (oper2 & 15)) & 1;
			if (mode == 3) {
				writerm16(rm, oper1 & ~(1 << (oper2 & 15)));
			}
			else {
				cpu_writew(ea, oper1 & ~(1 << (oper2 & 15)));
			}
		}
		break;
	case 7: //BTC rm16/32, imm8
		if (isoper32) {
			cf = (oper1_32 >> (oper2_32 & 31)) & 1;
			if (mode == 3) {
				writerm32(rm, oper1_32 ^ (1 << (oper2_32 & 31)));
			}
			else {
				cpu_writel(ea, oper1_32 ^ (1 << (oper2_32 & 31)));
			}
		}
		else {
			cf = (oper1 >> (oper2 & 15)) & 1;
			if (mode == 3) {
				writerm16(rm, oper1 ^ (1 << (oper2 & 15)));
			}
			else {
				cpu_writew(ea, oper1 ^ (1 << (oper2 & 15)));
			}
		}
		break;
	default:
		printf("0F BA reg=%u", reg);
		while(1);
	}
}

//BTC reg
void op_ext_BB() {
	modregrm();
	if (isoper32) {
		oper2_32 = getreg32(reg);
		if (mode == 3) {
			oper1_32 = readrm32(rm);
			writerm32(rm, oper1_32 ^ (1 << (oper2_32 & 31)));
		}
		else { //can read memory beyond 32 bits
			if (oper2_32 & 0x80000000) printf("negative 0xBB 32-bit");
			getea(rm);
			ea += (oper2_32 / 32) * 4;
			oper1_32 = cpu_readl(ea);
			cpu_writel(ea, oper1_32 ^ (1 << (oper2_32 & 31)));
		}
		cf = (oper1_32 >> (oper2_32 & 31)) & 1;
	}
	else {
		oper2 = getreg16(reg);
		if (mode == 3) {
			oper1 = readrm16(rm);
			writerm16(rm, oper1 ^ (1 << (oper2 & 15)));
		}
		else {
			if (oper2 & 0x8000) printf("negative 0xBB 16-bit");
			getea(rm);
			ea += (oper2 / 16) * 2;
			oper1 = cpu_readw(ea);
			cpu_writew(ea, oper1 ^ (1 << (oper2 & 15)));
		}
		cf = (oper1 >> (oper2 & 15)) & 1;
	}
}

//BSF
void op_ext_BC() {
	uint32_t src, temp, i;
	modregrm();
	if (isoper32) {
		src = readrm32(rm);
		if (src == 0) {
			zf = 1;
			return;
		}
		zf = 0;
		for (i = 0; i < 32; i++) {
			if (src & (1 << i)) break;
		}
		putreg32(reg, i);
	}
	else {
		src = readrm16(rm);
		if (src == 0) {
			zf = 1;
			return;
		}
		zf = 0;
		for (i = 0; i < 16; i++) {
			if (src & (1 << i)) break;
		}
		putreg16(reg, (uint16_t)i);
	}
}

//BSR
void op_ext_BD() {
	uint32_t src, temp;
	int i;
	modregrm();
	if (isoper32) {
		src = readrm32(rm);
		if (src == 0) {
			zf = 1;
			return;
		}
		zf = 0;
		for (i = 31; i >= 0; i--) {
			if (src & (1 << i)) break;
		}
		putreg32(reg, i);
	}
	else {
		src = readrm16(rm);
		if (src == 0) {
			zf = 1;
			return;
		}
		zf = 0;
		for (i = 15; i >= 0; i--) {
			if (src & (1 << i)) break;
		}
		putreg16(reg, (uint16_t)i);
	}
}

//MOVSX reg, rm8
void op_ext_BE() {
	modregrm();
	if (isoper32) {
		oper1_32 = (int32_t)(int16_t)(int8_t)readrm8(rm);
		putreg32(reg, oper1_32);
	}
	else {
		oper1 = (int16_t)(int8_t)readrm8(rm);
		putreg16(reg, oper1);
	}
}

//MOVSX reg, rm16
void op_ext_BF() {
	modregrm();
	oper1_32 = (int32_t)(int16_t)readrm16(rm);
	putreg32(reg, oper1_32);
}

//XADD r/m8, r8
void op_ext_C0() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_add8();
	writerm8(rm, res8);
	putreg8(reg, oper1b);
}

//XADD r/m16/32, r16/32
void op_ext_C1() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_add32();
		writerm32(rm, res32);
		putreg32(reg, oper1_32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_add16();
		writerm16(rm, res16);
		putreg16(reg, oper1);
	}
}

//BSWAP EAX
//BSWAP ECX
//BSWAP EDX
//BSWAP EBX
//BSWAP ESP
//BSWAP EBP
//BSWAP ESI
//BSWAP EDI
void op_ext_C8_C9_CA_CB_CC_CD_CE_CF() {
	uint8_t reg;
	uint32_t val;
	reg = opcode & 7;
	val = regs.longregs[reg];
	regs.longregs[reg] =
		((val & 0x000000FF) << 24) |
		((val & 0x0000FF00) << 8) |
		((val & 0x00FF0000) >> 8) |
		((val & 0xFF000000) >> 24);
}

void op_ext_illegal() {
	exception(6, 0); //UD
	debug_log(DEBUG_INFO, "[CPU] Invalid opcode exception at %08X (op 0F %02X)\r\n", segcache[regcs] + firstip, opcode);
}

void (*opcode_ext_table[256])() = {
	op_ext_00, op_ext_01, op_ext_02, op_ext_03, op_ext_illegal, op_ext_illegal, op_ext_06, op_ext_illegal,
	op_ext_08_09, op_ext_08_09, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_20, op_ext_21, op_ext_22, op_ext_23, op_ext_24_26, op_ext_illegal, op_ext_24_26, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_30, op_ext_31, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_40, op_ext_41, op_ext_42, op_ext_43, op_ext_44, op_ext_45, op_ext_46, op_ext_47,
	op_ext_48, op_ext_49, op_ext_4A, op_ext_4B, op_ext_4C, op_ext_4D, op_ext_4E, op_ext_4F,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_80, op_ext_81, op_ext_82, op_ext_83, op_ext_84, op_ext_85, op_ext_86, op_ext_87,
	op_ext_88, op_ext_89, op_ext_8A, op_ext_8B, op_ext_8C, op_ext_8D, op_ext_8E, op_ext_8F,
	op_ext_90, op_ext_91, op_ext_92, op_ext_93, op_ext_94, op_ext_95, op_ext_96, op_ext_97,
	op_ext_98, op_ext_99, op_ext_9A, op_ext_9B, op_ext_9C, op_ext_9D, op_ext_9E, op_ext_9F,
	op_ext_A0, op_ext_A1, op_ext_A2, op_ext_A3, op_ext_A4_A5, op_ext_A4_A5, op_ext_A6_B0, op_ext_illegal,
	op_ext_A8, op_ext_A9, op_ext_AA, op_ext_AB, op_ext_AC_AD, op_ext_AC_AD, op_ext_illegal, op_ext_AF,
	op_ext_A6_B0, op_ext_B1, op_ext_B2_B4_B5, op_ext_B3, op_ext_B2_B4_B5, op_ext_B2_B4_B5, op_ext_B6, op_ext_B7,
	op_ext_illegal, op_ext_illegal, op_ext_BA, op_ext_BB, op_ext_BC, op_ext_BD, op_ext_BE, op_ext_BF,
	op_ext_C0, op_ext_C1, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF, op_ext_C8_C9_CA_CB_CC_CD_CE_CF,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
	op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal, op_ext_illegal,
};

FUNC_INLINE void cpu_extop() {
	opcode = getmem8(segcache[regcs], ip);
	StepIP(1);

	(*opcode_ext_table[opcode])();
}

void fpu_exec();

/* 00 ADD Eb Gb */
void op_00() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_add8();
	writerm8(rm, res8);
}

/* 01 ADD Ev Gv */
void op_01() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_add32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_add16();
		writerm16(rm, res16);
	}
}

/* 02 ADD Gb Eb */
void op_02() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_add8();
	putreg8(reg, res8);
}

/* 03 ADD Gv Ev */
void op_03() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_add32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_add16();
		putreg16(reg, res16);
	}
}

/* 04 ADD regs.byteregs[regal] Ib */
void op_04() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_add8();
	regs.byteregs[regal] = res8;
}

/* 05 ADD eAX Iv */
void op_05() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_add32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_add16();
		regs.wordregs[regax] = res16;
	}
}

/* 06 PUSH segregs[reges] */
void op_06() {
	push(segregs[reges]);
}

/* 07 POP segregs[reges] */
void op_07() {
	putsegreg(reges, pop());
}

/* 08 OR Eb Gb */
void op_08() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_or8();
	writerm8(rm, res8);
}

/* 09 OR Ev Gv */
void op_09() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_or32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_or16();
		writerm16(rm, res16);
	}
}

/* 0A OR Gb Eb */
void op_0A() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_or8();
	putreg8(reg, res8);
}

/* 0B OR Gv Ev */
void op_0B() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_or32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_or16();
		//if ((oper1 == 0xF802) && (oper2 == 0xF802)) {
		//	sf = 0;	/* cheap hack to make Wolf 3D think we're a 286 so it plays */
		//}
		putreg16(reg, res16);
	}
}

/* 0C OR regs.byteregs[regal] Ib */
void op_0C() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_or8();
	regs.byteregs[regal] = res8;
}

/* 0D OR eAX Iv */
void op_0D() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_or32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_or16();
		regs.wordregs[regax] = res16;
	}
}

/* 0E PUSH segregs[regcs] */
void op_0E() {
	push(segregs[regcs]);
}

/* 10 ADC Eb Gb */
void op_10() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_adc8();
	writerm8(rm, res8);
}

/* 11 ADC Ev Gv */
void op_11() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_adc32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_adc16();
		writerm16(rm, res16);
	}
}

/* 12 ADC Gb Eb */
void op_12() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_adc8();
	putreg8(reg, res8);
}

/* 13 ADC Gv Ev */
void op_13() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_adc32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_adc16();
		putreg16(reg, res16);
	}
}

/* 14 ADC regs.byteregs[regal] Ib */
void op_14() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_adc8();
	regs.byteregs[regal] = res8;
}

/* 15 ADC eAX Iv */
void op_15() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_adc32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_adc16();
		regs.wordregs[regax] = res16;
	}
}

/* 16 PUSH segregs[regss] */
void op_16() {
	push(segregs[regss]);
}

/* 17 POP segregs[regss] */
void op_17() {
	putsegreg(regss, pop());
}

/* 18 SBB Eb Gb */
void op_18() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_sbb8();
	writerm8(rm, res8);
}

/* 19 SBB Ev Gv */
void op_19() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_sbb32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_sbb16();
		writerm16(rm, res16);
	}
}

/* 1A SBB Gb Eb */
void op_1A() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_sbb8();
	putreg8(reg, res8);
}

/* 1B SBB Gv Ev */
void op_1B() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_sbb32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_sbb16();
		putreg16(reg, res16);
	}
}

/* 1C SBB regs.byteregs[regal] Ib */
void op_1C() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_sbb8();
	regs.byteregs[regal] = res8;
}

/* 1D SBB eAX Iv */
void op_1D() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_sbb32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_sbb16();
		regs.wordregs[regax] = res16;
	}
}

/* 1E PUSH segregs[regds] */
void op_1E() {
	push(segregs[regds]);
}

/* 1F POP segregs[regds] */
void op_1F() {
	putsegreg(regds, pop());
}

/* 20 AND Eb Gb */
void op_20() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_and8();
	writerm8(rm, res8);
}

/* 21 AND Ev Gv */
void op_21() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_and32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_and16();
		writerm16(rm, res16);
	}
}

/* 22 AND Gb Eb */
void op_22() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_and8();
	putreg8(reg, res8);
}

/* 23 AND Gv Ev */
void op_23() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_and32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_and16();
		putreg16(reg, res16);
	}
}

/* 24 AND regs.byteregs[regal] Ib */
void op_24() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_and8();
	regs.byteregs[regal] = res8;
}

/* 25 AND eAX Iv */
void op_25() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_and32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_and16();
		regs.wordregs[regax] = res16;
	}
}

/* 27 DAA */
void op_27() {
	uint8_t old_al;
	old_al = regs.byteregs[regal];
	if (((regs.byteregs[regal] & 0x0F) > 9) || af) {
		oper1 = (uint16_t)regs.byteregs[regal] + 0x06;
		regs.byteregs[regal] = oper1 & 0xFF;
		if (oper1 & 0xFF00) cf = 1;
		if ((oper1 & 0x000F) < (old_al & 0x0F)) af = 1;
	}
	if (((regs.byteregs[regal] & 0xF0) > 0x90) || cf) {
		oper1 = (uint16_t)regs.byteregs[regal] + 0x60;
		regs.byteregs[regal] = oper1 & 0xFF;
		if (oper1 & 0xFF00) cf = 1; else cf = 0;
	}
	flag_szp8(regs.byteregs[regal]);
}

/* 28 SUB Eb Gb */
void op_28() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_sub8();
	writerm8(rm, res8);
}

/* 29 SUB Ev Gv */
void op_29() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_sub32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_sub16();
		writerm16(rm, res16);
	}
}

/* 2A SUB Gb Eb */
void op_2A() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_sub8();
	putreg8(reg, res8);
}

/* 2B SUB Gv Ev */
void op_2B() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_sub32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_sub16();
		putreg16(reg, res16);
	}
}

/* 2C SUB regs.byteregs[regal] Ib */
void op_2C() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_sub8();
	regs.byteregs[regal] = res8;
}

/* 2D SUB eAX Iv */
void op_2D() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_sub32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_sub16();
		regs.wordregs[regax] = res16;
	}
}

/* 2F DAS */
void op_2F() {
	uint8_t old_al;
	old_al = regs.byteregs[regal];
	if (((regs.byteregs[regal] & 0x0F) > 9) || af) {
		oper1 = (uint16_t)regs.byteregs[regal] - 0x06;
		regs.byteregs[regal] = oper1 & 0xFF;
		if (oper1 & 0xFF00) cf = 1;
		if ((oper1 & 0x000F) >= (old_al & 0x0F)) af = 1;
	}
	if (((regs.byteregs[regal] & 0xF0) > 0x90) || cf) {
		oper1 = (uint16_t)regs.byteregs[regal] - 0x60;
		regs.byteregs[regal] = oper1 & 0xFF;
		if (oper1 & 0xFF00) cf = 1; else cf = 0;
	}
	flag_szp8(regs.byteregs[regal]);
}

/* 30 XOR Eb Gb */
void op_30() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	op_xor8();
	writerm8(rm, res8);

}

/* 31 XOR Ev Gv */
void op_31() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		op_xor32();
		writerm32(rm, res32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		op_xor16();
		writerm16(rm, res16);
	}
}

/* 32 XOR Gb Eb */
void op_32() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	op_xor8();
	putreg8(reg, res8);
}

/* 33 XOR Gv Ev */
void op_33() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		op_xor32();
		putreg32(reg, res32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		op_xor16();
		putreg16(reg, res16);
	}
}

/* 34 XOR regs.byteregs[regal] Ib */
void op_34() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	op_xor8();
	regs.byteregs[regal] = res8;
}

/* 35 XOR eAX Iv */
void op_35() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		op_xor32();
		regs.longregs[regeax] = res32;
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		op_xor16();
		regs.wordregs[regax] = res16;
	}
}

/* 37 AAA ASCII */
void op_37() {
	if (((regs.byteregs[regal] & 0xF) > 9) || (af == 1)) {
		regs.wordregs[regax] = regs.wordregs[regax] + 0x106;
		af = 1;
		cf = 1;
	}
	else {
		af = 0;
		cf = 0;
	}

	regs.byteregs[regal] = regs.byteregs[regal] & 0xF;
}

/* 38 CMP Eb Gb */
void op_38() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getreg8(reg);
	flag_sub8(oper1b, oper2b);
}

/* 39 CMP Ev Gv */
void op_39() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getreg32(reg);
		flag_sub32(oper1_32, oper2_32);
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getreg16(reg);
		//printf("cmp %04X, %04X", oper1, oper2);
		flag_sub16(oper1, oper2);
	}
}

/* 3A CMP Gb Eb */
void op_3A() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	flag_sub8(oper1b, oper2b);
}

/* 3B CMP Gv Ev */
void op_3B() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		flag_sub32(oper1_32, oper2_32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		flag_sub16(oper1, oper2);
	}
}

/* 3C CMP regs.byteregs[regal] Ib */
void op_3C() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	flag_sub8(oper1b, oper2b);
}

/* 3D CMP eAX Iv */
void op_3D() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		flag_sub32(oper1_32, oper2_32);
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		flag_sub16(oper1, oper2);
	}
}

/* 3F AAS ASCII */
void op_3F() {
	if (((regs.byteregs[regal] & 0xF) > 9) || (af == 1)) {
		regs.wordregs[regax] = regs.wordregs[regax] - 6;
		regs.byteregs[regah] = regs.byteregs[regah] - 1;
		af = 1;
		cf = 1;
	}
	else {
		af = 0;
		cf = 0;
	}

	regs.byteregs[regal] = regs.byteregs[regal] & 0xF;
}

/* INC AX */
/* INC CX */
/* INC DX */
/* INC BX */
/* INC SP */
/* INC BP */
/* INC SI */
/* INC DI */
void op_inc() {
	if (isoper32) {
		oldcf = cf;
		oper1_32 = getreg32(opcode & 7);
		oper2_32 = 1;
		op_add32();
		cf = oldcf;
		putreg32(opcode & 7, res32);
	}
	else {
		oldcf = cf;
		oper1 = getreg16(opcode & 7);
		oper2 = 1;
		op_add16();
		cf = oldcf;
		putreg16(opcode & 7, res16);
	}
}

/* DEC AX */
/* DEC CX */
/* DEC DX */
/* DEC BX */
/* DEC SP */
/* DEC BP */
/* DEC SI */
/* DEC DI */
void op_dec() {
	if (isoper32) {
		oldcf = cf;
		oper1_32 = getreg32(opcode & 7);
		oper2_32 = 1;
		op_sub32();
		cf = oldcf;
		putreg32(opcode & 7, res32);
	}
	else {
		oldcf = cf;
		oper1 = getreg16(opcode & 7);
		oper2 = 1;
		op_sub16();
		cf = oldcf;
		putreg16(opcode & 7, res16);
	}
}

/* 50 PUSH eAX */
/* 51 PUSH eCX */
/* 52 PUSH eDX */
/* 53 PUSH eBX */
/* 55 PUSH eBP */
/* 56 PUSH eSI */
/* 57 PUSH eDI */
void op_push() {
	push(isoper32 ? getreg32(opcode & 7) : getreg16(opcode & 7));
}

/* 54 PUSH eSP */
void op_54() {
	push(isoper32 ? getreg32(opcode & 7) : getreg16(opcode & 7));
}

/* 58 POP eAX */
/* 59 POP eCX */
/* 5A POP eDX */
/* 5B POP eBX */
/* 5C POP eSP */
/* 5D POP eBP */
/* 5E POP eSI */
/* 5F POP eDI */
void op_pop() {
	if (isoper32) {
		putreg32(opcode & 7, pop());
		/*if (opcode == 0x5C) { //TODO: is this stuff right?
			if (segis32[regss])
				regs.longregs[regesp] += 4;
			else
				regs.wordregs[regsp] += 4;
		}*/
	}
	else {
		putreg16(opcode & 7, pop());
		/*if (opcode == 0x5C) {
			if (segis32[regss])
				regs.longregs[regesp] += 2;
			else
				regs.wordregs[regsp] += 2;
		}*/
	}
}

/* 60 PUSHA (80186+) */
void op_60() {
	if (isoper32) {
		oldsp = regs.longregs[regesp];
		push(regs.longregs[regeax]);
		push(regs.longregs[regecx]);
		push(regs.longregs[regedx]);
		push(regs.longregs[regebx]);
		push(oldsp);
		push(regs.longregs[regebp]);
		push(regs.longregs[regesi]);
		push(regs.longregs[regedi]);
	}
	else {
		oldsp = regs.wordregs[regsp];
		push(regs.wordregs[regax]);
		push(regs.wordregs[regcx]);
		push(regs.wordregs[regdx]);
		push(regs.wordregs[regbx]);
		push(oldsp);
		push(regs.wordregs[regbp]);
		push(regs.wordregs[regsi]);
		push(regs.wordregs[regdi]);
	}
}

/* 61 POPA (80186+) */
void op_61() {
	if (isoper32) {
		uint32_t dummy;
		regs.longregs[regedi] = pop();
		regs.longregs[regesi] = pop();
		regs.longregs[regebp] = pop();
		dummy = pop();
		regs.longregs[regebx] = pop();
		regs.longregs[regedx] = pop();
		regs.longregs[regecx] = pop();
		regs.longregs[regeax] = pop();
	}
	else {
		uint16_t dummy;
		regs.wordregs[regdi] = pop();
		regs.wordregs[regsi] = pop();
		regs.wordregs[regbp] = pop();
		dummy = pop();
		regs.wordregs[regbx] = pop();
		regs.wordregs[regdx] = pop();
		regs.wordregs[regcx] = pop();
		regs.wordregs[regax] = pop();
	}
}

/* 62 BOUND Gv, Ev (80186+) */
void op_62() {
	modregrm();
	getea(rm);
	if (isoper32) {
		int32_t index, lower_bound, upper_bound;
		index = (int32_t)getreg32(reg);
		lower_bound = (int32_t)cpu_readl(ea);
		upper_bound = (int32_t)cpu_readl(ea + 4);
		if ((index < lower_bound) || (index > upper_bound)) {
			//cpu_intcall(5, INT_SOURCE_EXCEPTION, 0); //bounds check exception
			exception(5, 0); //BR
		}
	}
	else {
		int16_t index, lower_bound, upper_bound;
		index = (int16_t)getreg16(reg);
		lower_bound = (int16_t)cpu_readw(ea);
		upper_bound = (int16_t)cpu_readw(ea + 2);
		if ((index < lower_bound) || (index > upper_bound)) {
			//cpu_intcall(5, INT_SOURCE_EXCEPTION, 0); //bounds check exception
			exception(5, 0); //BR
		}
	}
}


void op_63() {
	modregrm();
	oper1 = readrm16(rm);
	oper2 = getreg16(reg) & 0x03;
	if ((oper1 & 3) < oper2) {
		res16 = (oper1 & ~0x03) | oper2;
		writerm16(rm, res16);
		zf = 1;
	}
	else {
		zf = 0;
	}
}

/* 68 PUSH Iv (80186+) */
void op_68() {
	if (isoper32) {
		push(getmem32(segcache[regcs], ip));
		StepIP(4);
	}
	else {
		push(getmem16(segcache[regcs], ip));
		StepIP(2);
	}
}

/* 69 IMUL Gv Ev Iv (80186+) */
void op_69() {
	modregrm();
	if (isoper32) {
		int32_t src1 = (int32_t)readrm32(rm);
		int32_t src2 = (int32_t)getmem32(segcache[regcs], ip);
		StepIP(4);

		int64_t result = (int64_t)src1 * (int64_t)src2;
		putreg32(reg, (uint32_t)(result & 0xFFFFFFFF));

		// Check for signed overflow: if result doesn't fit in 32 bits
		if ((result >> 31) != 0 && (result >> 31) != -1) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
		/*oper1_32 = readrm32(rm);
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		if ((oper1_32 & 0x80000000) == 0x80000000) {
			temp64 = oper1_32 | 0xFFFFFFFF00000000UL;
		}

		if ((oper2_32 & 0x80000000) == 0x80000000) {
			temp64_2 = oper2_32 | 0xFFFFFFFF00000000UL;
		}

		temp64 = (uint64_t)oper1_32 * (uint64_t)oper2_32;
		putreg32(reg, temp64 & 0xFFFFFFFF);
		if (temp64 & 0xFFFFFFFF00000000UL) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}*/
	}
	else {
		temp1 = readrm16(rm);
		temp2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		if ((temp1 & 0x8000L) == 0x8000L) {
			temp1 = temp1 | 0xFFFF0000L;
		}

		if ((temp2 & 0x8000L) == 0x8000L) {
			temp2 = temp2 | 0xFFFF0000L;
		}

		temp3 = temp1 * temp2;
		putreg16(reg, temp3 & 0xFFFFL);
		if (temp3 & 0xFFFF0000L) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
	}
}

/* 6A PUSH Ib (80186+) */
void op_6A() {
	push(signext8to32(getmem8(segcache[regcs], ip)));
	StepIP(1);
}

/* 6B IMUL Gv Eb Ib (80186+) */
void op_6B() {
	modregrm();
	if (isoper32) {
		temp64 = (int64_t)(int32_t)readrm32(rm);
		temp64_2 = (int64_t)(int8_t)getmem8(segcache[regcs], ip);
		StepIP(1);

		temp64_3 = temp64 * temp64_2;
		putreg32(reg, temp64_3 & 0xFFFFFFFFL);
		if (temp64_3 & 0xFFFFFFFF00000000LL) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
	}
	else {
		temp1 = readrm16(rm);
		temp2 = signext(getmem8(segcache[regcs], ip));
		StepIP(1);
		if ((temp1 & 0x8000L) == 0x8000L) {
			temp1 = temp1 | 0xFFFF0000L;
		}

		if ((temp2 & 0x8000L) == 0x8000L) {
			temp2 = temp2 | 0xFFFF0000L;
		}

		temp3 = temp1 * temp2;
		putreg16(reg, temp3 & 0xFFFFL);
		if (temp3 & 0xFFFF0000L) {
			cf = 1;
			of = 1;
		}
		else {
			cf = 0;
			of = 0;
		}
	}
}

/* 6E INSB */
void op_6C() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isaddr32) {
		putmem8(segcache[reges], regs.longregs[regedi], port_read(regs.wordregs[regdx]));
	}
	else {
		putmem8(segcache[reges], regs.wordregs[regdi], port_read(regs.wordregs[regdx]));
	}

	if (df) {
		if (isaddr32) {
			//regs.longregs[regesi]--;
			regs.longregs[regedi]--;
		}
		else {
			//regs.wordregs[regsi]--;
			regs.wordregs[regdi]--;
		}
	}
	else {
		if (isaddr32) {
			//regs.longregs[regesi]++;
			regs.longregs[regedi]++;
		}
		else {
			//regs.wordregs[regsi]++;
			regs.wordregs[regdi]++;
		}
	}

	if (reptype) {
		if (isaddr32) {
			regs.longregs[regecx]--;
		}
		else {
			regs.wordregs[regcx]--;
		}
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* 6D INSW */
void op_6D() {
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isoper32) {
		if (isaddr32)
			putmem32(segcache[reges], regs.longregs[regedi], port_readl(regs.wordregs[regdx]));
		else
			putmem32(segcache[reges], regs.wordregs[regdi], port_readl(regs.wordregs[regdx]));
	}
	else {
		if (isaddr32)
			putmem16(segcache[reges], regs.longregs[regedi], port_readw(regs.wordregs[regdx]));
		else
			putmem16(segcache[reges], regs.wordregs[regdi], port_readw(regs.wordregs[regdx]));
	}
	if (df) {
		if (isaddr32) {
			regs.longregs[regedi] -= isoper32 ? 4 : 2;
			//regs.longregs[regesi] -= isoper32 ? 4 : 2;
		}
		else {
			regs.wordregs[regdi] -= isoper32 ? 4 : 2;
			//regs.wordregs[regsi] -= isoper32 ? 4 : 2;
		}
	}
	else {
		if (isaddr32) {
			regs.longregs[regedi] += isoper32 ? 4 : 2;
			//regs.longregs[regesi] += isoper32 ? 4 : 2;
		}
		else {
			regs.wordregs[regdi] += isoper32 ? 4 : 2;
			//regs.wordregs[regsi] += isoper32 ? 4 : 2;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* 6E OUTSB */
void op_6E() {
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isaddr32) {
		port_write(regs.wordregs[regdx], getmem8(useseg, regs.longregs[regesi]));
	}
	else {
		port_write(regs.wordregs[regdx], getmem8(useseg, regs.wordregs[regsi]));
	}

	if (df) {
		if (isaddr32) {
			regs.longregs[regesi]--;
		}
		else {
			regs.wordregs[regsi]--;
		}
	}
	else {
		if (isaddr32) {
			regs.longregs[regesi]++;
		}
		else {
			regs.wordregs[regsi]++;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* 6F OUTSW */
void op_6F() {
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isoper32) {
		if (isaddr32)
			port_writel(regs.wordregs[regdx], getmem32(useseg, regs.longregs[regesi]));
		else
			port_writel(regs.wordregs[regdx], getmem32(useseg, regs.wordregs[regsi]));
	}
	else {
		if (isaddr32)
			port_writew(regs.wordregs[regdx], getmem16(useseg, regs.longregs[regesi]));
		else
			port_writew(regs.wordregs[regdx], getmem16(useseg, regs.wordregs[regsi]));
	}
	if (df) {
		if (isaddr32) {
			//regs.longregs[regedi] -= isoper32 ? 4 : 2;
			regs.longregs[regesi] -= isoper32 ? 4 : 2;
		}
		else {
			//regs.wordregs[regdi] -= isoper32 ? 4 : 2;
			regs.wordregs[regsi] -= isoper32 ? 4 : 2;
		}
	}
	else {
		if (isaddr32) {
			//regs.longregs[regedi] += isoper32 ? 4 : 2;
			regs.longregs[regesi] += isoper32 ? 4 : 2;
		}
		else {
			//regs.wordregs[regdi] += isoper32 ? 4 : 2;
			regs.wordregs[regsi] += isoper32 ? 4 : 2;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* 70 JO Jb */
void op_70() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (of) {
		ip = ip + temp32;
	}
}

/* 71 JNO Jb */
void op_71() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (!of) {
		ip = ip + temp32;
	}
}

/* 72 JB Jb */
void op_72() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (cf) {
		ip = ip + temp32;
	}
}

/* 73 JNB Jb */
void op_73() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (!cf) {
		ip = ip + temp32;
	}
}

/* 74 JZ Jb */
void op_74() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (zf) {
		ip = ip + temp32;
	}
}

/* 75 JNZ Jb */
void op_75() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (segregs[regcs] == 0xF000 && ip == 0x13C9) return; //TODO: Hack for award486 BIOS. Fix it! Seems PIT related.
	if (segregs[regcs] == 0xE000 && ip == 0xCC2A) return; //TODO: Hack for p5sp4 BIOS. Fix it! Seems PIT related.
	if (!zf) {
		ip = ip + temp32;
	}
}

/* 76 JBE Jb */
void op_76() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (cf || zf) {
		ip = ip + temp32;
	}
}

/* 77 JA Jb */
void op_77() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (!cf && !zf) {
		ip = ip + temp32;
	}
}

/* 78 JS Jb */
void op_78() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (sf) {
		ip = ip + temp32;
	}
}

/* 79 JNS Jb */
void op_79() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (!sf) {
		ip = ip + temp32;
	}
}

/* 7A JPE Jb */
void op_7A() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (pf) {
		ip = ip + temp32;
	}
}

/* 7B JPO Jb */
void op_7B() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (!pf) {
		ip = ip + temp32;
	}
}

/* 7C JL Jb */
void op_7C() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (sf != of) {
		ip = ip + temp32;
	}
}

/* 7D JGE Jb */
void op_7D() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (sf == of) {
		ip = ip + temp32;
	}
}

/* 7E JLE Jb */
void op_7E() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if ((sf != of) || zf) {
		ip = ip + temp32;
	}
}

/* 7F JG Jb */
void op_7F() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (!zf && (sf == of)) {
		ip = ip + temp32;
	}
}

/* 80/82 GRP1 Eb Ib */
void op_80_82() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	switch (reg) {
	case 0:
		op_add8();
		break;
	case 1:
		op_or8();
		break;
	case 2:
		op_adc8();
		break;
	case 3:
		op_sbb8();
		break;
	case 4:
		op_and8();
		break;
	case 5:
		op_sub8();
		break;
	case 6:
		op_xor8();
		break;
	case 7:
		flag_sub8(oper1b, oper2b);
		break;
	default:
		break;	/* to avoid compiler warnings */
	}

	if (reg < 7) {
		writerm8(rm, res8);
	}
}

/* 81 GRP1 Ev Iv */
/* 83 GRP1 Ev Ib */
void op_81_83() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		if (opcode == 0x81) {
			oper2_32 = getmem32(segcache[regcs], ip);
			StepIP(4);
		}
		else {
			oper2_32 = signext8to32(getmem8(segcache[regcs], ip));
			StepIP(1);
		}
	}
	else {
		oper1 = readrm16(rm);
		if (opcode == 0x81) {
			oper2 = getmem16(segcache[regcs], ip);
			StepIP(2);
		}
		else {
			oper2 = signext(getmem8(segcache[regcs], ip));
			StepIP(1);
		}
	}

	switch (reg) {
	case 0:
		if (isoper32) op_add32(); else op_add16();
		break;
	case 1:
		if (isoper32) op_or32(); else op_or16();
		break;
	case 2:
		if (isoper32) op_adc32(); else op_adc16();
		break;
	case 3:
		if (isoper32) op_sbb32(); else op_sbb16();
		break;
	case 4:
		if (isoper32) op_and32(); else op_and16();
		break;
	case 5:
		if (isoper32) op_sub32(); else op_sub16();
		break;
	case 6:
		if (isoper32) op_xor32(); else op_xor16();
		break;
	case 7:
		if (isoper32) flag_sub32(oper1_32, oper2_32); else flag_sub16(oper1, oper2);
		break;
	default:
		break;	/* to avoid compiler warnings */
	}

	if (reg < 7) {
		if (isoper32) {
			writerm32(rm, res32);
		}
		else {
			writerm16(rm, res16);
		}
	}
}

/* 84 TEST Gb Eb */
void op_84() {
	modregrm();
	oper1b = getreg8(reg);
	oper2b = readrm8(rm);
	flag_log8(oper1b & oper2b);
}

/* 85 TEST Gv Ev */
void op_85() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		oper2_32 = readrm32(rm);
		flag_log32(oper1_32 & oper2_32);
	}
	else {
		oper1 = getreg16(reg);
		oper2 = readrm16(rm);
		flag_log16(oper1 & oper2);
	}
}

/* 86 XCHG Gb Eb */
void op_86() {
	modregrm();
	oper1b = getreg8(reg);
	putreg8(reg, readrm8(rm));
	writerm8(rm, oper1b);
}

/* 87 XCHG Gv Ev */
void op_87() {
	modregrm();
	if (isoper32) {
		oper1_32 = getreg32(reg);
		putreg32(reg, readrm32(rm));
		writerm32(rm, oper1_32);
	}
	else {
		oper1 = getreg16(reg);
		putreg16(reg, readrm16(rm));
		writerm16(rm, oper1);
	}
}

/* 88 MOV Eb Gb */
void op_88() {
	modregrm();
	writerm8(rm, getreg8(reg));
}

/* 89 MOV Ev Gv */
void op_89() {
	modregrm();
	if (isoper32) {
		writerm32(rm, getreg32(reg));
	}
	else {
		writerm16(rm, getreg16(reg));
	}
}

/* 8A MOV Gb Eb */
void op_8A() {
	modregrm();
	putreg8(reg, readrm8(rm));
}

/* 8B MOV Gv Ev */
void op_8B() {
	modregrm();
	if (isoper32) {
		putreg32(reg, readrm32(rm));
	}
	else {
		putreg16(reg, readrm16(rm));
	}
}

/* 8C MOV Ew Sw */
void op_8C() {
	modregrm();
	writerm16(rm, getsegreg(reg));
}

/* 8D LEA Gv M */
void op_8D() {
	modregrm();
	getea(rm);
	if (isoper32) { //TODO: is this right for 32 bit?
		putreg32(reg, ea - useseg); // segbase(useseg));
	}
	else {
		putreg16(reg, ea - useseg); // segbase(useseg));
	}
}

/* 8E MOV Sw Ew */
void op_8E() {
	//if (isoper32 || isaddr32) { printf("32-bit op attempt on %02X", opcode); while(1); }
	modregrm();
	if (reg == regcs) {
		//cpu_intcall(6, INT_SOURCE_EXCEPTION, 0); //UD
		ip = firstip;
		exception(6, 0); //UD
		return;
	}
	putsegreg(reg, readrm16(rm));
}

/* 8F POP Ev */
void op_8F() {
	if (isoper32) {
		shadow_esp += 4;
		modregrm();
		writerm32(rm, pop());
		//debug_log(DEBUG_DETAIL, "EA was %08X", ea - useseg);
	}
	else {
		modregrm();
		writerm16(rm, pop());
	}
}

/* 90 NOP (technically XCHG eAX eAX) */
void op_90() {

}

/* 91 XCHG eCX eAX */
/* 92 XCHG eDX eAX */
/* 93 XCHG eBX eAX */
/* 94 XCHG eSP eAX */
/* 95 XCHG eBP eAX */
/* 96 XCHG eSI eAX */
/* 97 XCHG eDI eAX */
void op_xchg() {
	if (isoper32) {
		oper1_32 = getreg32(opcode & 7);
		putreg32(opcode & 7, regs.longregs[regeax]);
		regs.longregs[regeax] = oper1_32;
	}
	else {
		oper1 = getreg16(opcode & 7);
		putreg16(opcode & 7, regs.wordregs[regax]);
		regs.wordregs[regax] = oper1;
	}
}

/* 98 CBW */
void op_98() {
	if (isoper32) {
		if ((regs.wordregs[regax] & 0x8000) == 0x8000) {
			regs.longregs[regeax] |= 0xFFFF0000;
		}
		else {
			regs.longregs[regeax] &= 0x0000FFFF;
		}
	}
	else {
		if ((regs.byteregs[regal] & 0x80) == 0x80) {
			regs.byteregs[regah] = 0xFF;
		}
		else {
			regs.byteregs[regah] = 0;
		}
	}
}

/* 99 CWD */
void op_99() {
	if (isoper32) {
		if (regs.longregs[regeax] & 0x80000000) {
			regs.longregs[regedx] = 0xFFFFFFFF;
		}
		else {
			regs.longregs[regedx] = 0;
		}
	}
	else {
		if ((regs.byteregs[regah] & 0x80) == 0x80) {
			regs.wordregs[regdx] = 0xFFFF;
		}
		else {
			regs.wordregs[regdx] = 0;
		}
	}
}

/* 9A CALL Ap */
void op_9A() {
	if (isoper32) {
		oper1_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		//pushl(segregs[regcs]);
		//pushl(ip);
	}
	else {
		oper1_32 = getmem16(segcache[regcs], ip);
		StepIP(2);
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		//pushw(segregs[regcs]);
		//pushw(ip);
	}
	cpu_callf(oper2, oper1_32);
	//ip = oper1_32;
	//putsegreg(regcs, oper2);
}

/* 9B WAIT */
void op_9B() {

}

/* 9C PUSHF */
void op_9C() {
	push(makeflagsword());
}

/* 9D POPF */
void op_9D() {
	if (isoper32) {
		temp32 = pop();
		decodeflagsword(temp32);
	}
	else {
		temp32 = pop();
		decodeflagsword((makeflagsword() & 0xFFFF0000) | (temp32 & 0xFFFF));
	}
}

/* 9E SAHF */
void op_9E() {
	decodeflagsword((makeflagsword() & 0xFFFFFF00) | regs.byteregs[regah]);
}

/* 9F LAHF */
void op_9F() {
	regs.byteregs[regah] = makeflagsword() & 0xFF;
}

/* A0 MOV regs.byteregs[regal] Ob */
void op_A0() {
	if (isaddr32) {
		regs.byteregs[regal] = getmem8(useseg, getmem32(segcache[regcs], ip));
		StepIP(4);
	}
	else {
		regs.byteregs[regal] = getmem8(useseg, getmem16(segcache[regcs], ip));
		StepIP(2);
	}
}

/* A1 MOV eAX Ov */
void op_A1() {
	if (isaddr32) {
		tempaddr32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		tempaddr32 = getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (isoper32) {
		oper1_32 = getmem32(useseg, tempaddr32);
		regs.longregs[regeax] = oper1_32;
	}
	else {
		oper1 = getmem16(useseg, tempaddr32);
		regs.wordregs[regax] = oper1;
	}
}

/* A2 MOV Ob regs.byteregs[regal] */
void op_A2() {
	if (isaddr32) {
		putmem8(useseg, getmem32(segcache[regcs], ip), regs.byteregs[regal]);
		StepIP(4);
	}
	else {
		putmem8(useseg, getmem16(segcache[regcs], ip), regs.byteregs[regal]);
		StepIP(2);
	}
}

/* A3 MOV Ov eAX */
void op_A3() {
	if (isaddr32) {
		tempaddr32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		tempaddr32 = getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	if (isoper32) {
		putmem32(useseg, tempaddr32, regs.longregs[regeax]);
	}
	else {
		putmem16(useseg, tempaddr32, regs.wordregs[regax]);
	}
}

/* A4 MOVSB */
void op_A4() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isaddr32)
		putmem8(segcache[reges], regs.longregs[regedi], getmem8(useseg, regs.longregs[regesi]));
	else
		putmem8(segcache[reges], regs.wordregs[regdi], getmem8(useseg, regs.wordregs[regsi]));

	if (df) {
		if (isaddr32) {
			regs.longregs[regesi]--;
			regs.longregs[regedi]--;
		}
		else {
			regs.wordregs[regsi]--;
			regs.wordregs[regdi]--;
		}
	}
	else {
		if (isaddr32) {
			regs.longregs[regesi]++;
			regs.longregs[regedi]++;
		}
		else {
			regs.wordregs[regsi]++;
			regs.wordregs[regdi]++;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* A5 MOVSW */
void op_A5() {
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isoper32) {
		if (isaddr32)
			putmem32(segcache[reges], regs.longregs[regedi], getmem32(useseg, regs.longregs[regesi]));
		else
			putmem32(segcache[reges], regs.wordregs[regdi], getmem32(useseg, regs.wordregs[regsi]));
	}
	else {
		if (isaddr32)
			putmem16(segcache[reges], regs.longregs[regedi], getmem16(useseg, regs.longregs[regesi]));
		else
			putmem16(segcache[reges], regs.wordregs[regdi], getmem16(useseg, regs.wordregs[regsi]));
	}

	if (df) {
		if (isaddr32) {
			regs.longregs[regedi] -= isoper32 ? 4 : 2;
			regs.longregs[regesi] -= isoper32 ? 4 : 2;
		}
		else {
			regs.wordregs[regdi] -= isoper32 ? 4 : 2;
			regs.wordregs[regsi] -= isoper32 ? 4 : 2;
		}
	}
	else {
		if (isaddr32) {
			regs.longregs[regedi] += isoper32 ? 4 : 2;
			regs.longregs[regesi] += isoper32 ? 4 : 2;
		}
		else {
			regs.wordregs[regdi] += isoper32 ? 4 : 2;
			regs.wordregs[regsi] += isoper32 ? 4 : 2;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* A6 CMPSB */
void op_A6() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isaddr32) {
		oper1b = getmem8(useseg, regs.longregs[regesi]);
		oper2b = getmem8(segcache[reges], regs.longregs[regedi]);
	}
	else {
		oper1b = getmem8(useseg, regs.wordregs[regsi]);
		oper2b = getmem8(segcache[reges], regs.wordregs[regdi]);
	}
	flag_sub8(oper1b, oper2b);

	if (df) {
		if (isaddr32) {
			regs.longregs[regesi]--;
			regs.longregs[regedi]--;
		}
		else {
			regs.wordregs[regsi]--;
			regs.wordregs[regdi]--;
		}
	}
	else {
		if (isaddr32) {
			regs.longregs[regesi]++;
			regs.longregs[regedi]++;
		}
		else {
			regs.wordregs[regsi]++;
			regs.wordregs[regdi]++;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if ((reptype == 1) && !zf) {
		return;
	}
	else if ((reptype == 2) && zf) {
		return;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* A7 CMPSW */
void op_A7() {
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isoper32) {
		if (isaddr32) {
			oper1_32 = getmem32(useseg, regs.longregs[regesi]);
			oper2_32 = getmem32(segcache[reges], regs.longregs[regedi]);
		}
		else {
			oper1_32 = getmem32(useseg, regs.wordregs[regsi]);
			oper2_32 = getmem32(segcache[reges], regs.wordregs[regdi]);
		}
		flag_sub32(oper1_32, oper2_32);
	}
	else {
		if (isaddr32) {
			oper1 = getmem16(useseg, regs.longregs[regesi]);
			oper2 = getmem16(segcache[reges], regs.longregs[regedi]);
		}
		else {
			oper1 = getmem16(useseg, regs.wordregs[regsi]);
			oper2 = getmem16(segcache[reges], regs.wordregs[regdi]);
		}
		flag_sub16(oper1, oper2);
	}

	if (df) {
		if (isaddr32) {
			regs.longregs[regedi] -= isoper32 ? 4 : 2;
			regs.longregs[regesi] -= isoper32 ? 4 : 2;
		}
		else {
			regs.wordregs[regdi] -= isoper32 ? 4 : 2;
			regs.wordregs[regsi] -= isoper32 ? 4 : 2;
		}
	}
	else {
		if (isaddr32) {
			regs.longregs[regedi] += isoper32 ? 4 : 2;
			regs.longregs[regesi] += isoper32 ? 4 : 2;
		}
		else {
			regs.wordregs[regdi] += isoper32 ? 4 : 2;
			regs.wordregs[regsi] += isoper32 ? 4 : 2;
		}
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if ((reptype == 1) && !zf) {
		return;
	}

	if ((reptype == 2) && zf) {
		return;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* A8 TEST regs.byteregs[regal] Ib */
void op_A8() {
	oper1b = regs.byteregs[regal];
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	flag_log8(oper1b & oper2b);
}

/* A9 TEST eAX Iv */
void op_A9() {
	if (isoper32) {
		oper1_32 = regs.longregs[regeax];
		oper2_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		flag_log32(oper1_32 & oper2_32);
	}
	else {
		oper1 = regs.wordregs[regax];
		oper2 = getmem16(segcache[regcs], ip);
		StepIP(2);
		flag_log16(oper1 & oper2);
	}
}

/* AA STOSB */
void op_AA() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isaddr32)
		putmem8(segcache[reges], regs.longregs[regedi], regs.byteregs[regal]);
	else
		putmem8(segcache[reges], regs.wordregs[regdi], regs.byteregs[regal]);

	if (df) {
		if (isaddr32)
			regs.longregs[regedi]--;
		else
			regs.wordregs[regdi]--;
	}
	else {
		if (isaddr32)
			regs.longregs[regedi]++;
		else
			regs.wordregs[regdi]++;
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* AB STOSW */
void op_AB() {
	//if (isoper32 || isaddr32) { printf("32-bit op attempt on %02X", opcode); while(1); }
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isoper32) {
		if (isaddr32)
			putmem32(segcache[reges], regs.longregs[regedi], regs.longregs[regeax]);
		else
			putmem32(segcache[reges], regs.wordregs[regdi], regs.longregs[regeax]);
	}
	else {
		if (isaddr32)
			putmem16(segcache[reges], regs.longregs[regedi], regs.wordregs[regax]);
		else
			putmem16(segcache[reges], regs.wordregs[regdi], regs.wordregs[regax]);
	}
	if (df) {
		if (isaddr32)
			regs.longregs[regedi] -= isoper32 ? 4 : 2;
		else
			regs.wordregs[regdi] -= isoper32 ? 4 : 2;
	}
	else {
		if (isaddr32)
			regs.longregs[regedi] += isoper32 ? 4 : 2;
		else
			regs.wordregs[regdi] += isoper32 ? 4 : 2;
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* AC LODSB */
void op_AC() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isaddr32)
		regs.byteregs[regal] = getmem8(useseg, regs.longregs[regesi]);
	else
		regs.byteregs[regal] = getmem8(useseg, regs.wordregs[regsi]);

	if (df) {
		if (isaddr32)
			regs.longregs[regesi]--;
		else
			regs.wordregs[regsi]--;
	}
	else {
		if (isaddr32)
			regs.longregs[regesi]++;
		else
			regs.wordregs[regsi]++;
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* AD LODSW */
void op_AD() {
	if (reptype) {
		if (isaddr32) {
			if (regs.longregs[regecx] == 0) return;
		}
		else {
			if (regs.wordregs[regcx] == 0) return;
		}
	}

	if (isoper32) {
		if (isaddr32)
			regs.longregs[regeax] = getmem32(useseg, regs.longregs[regesi]);
		else
			regs.longregs[regeax] = getmem32(useseg, regs.wordregs[regsi]);
	}
	else {
		if (isaddr32)
			regs.wordregs[regax] = getmem16(useseg, regs.longregs[regesi]);
		else
			regs.wordregs[regax] = getmem16(useseg, regs.wordregs[regsi]);
	}
	if (df) {
		if (isaddr32)
			regs.longregs[regesi] -= isoper32 ? 4 : 2;
		else
			regs.wordregs[regsi] -= isoper32 ? 4 : 2;
	}
	else {
		if (isaddr32)
			regs.longregs[regesi] += isoper32 ? 4 : 2;
		else
			regs.wordregs[regsi] += isoper32 ? 4 : 2;
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* AE SCASB */
void op_AE() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isaddr32) {
		oper1b = regs.byteregs[regal];
		oper2b = getmem8(segcache[reges], regs.longregs[regedi]);
	}
	else {
		oper1b = regs.byteregs[regal];
		oper2b = getmem8(segcache[reges], regs.wordregs[regdi]);
	}
	flag_sub8(oper1b, oper2b);

	if (df) {
		if (isaddr32)
			regs.longregs[regedi]--;
		else
			regs.wordregs[regdi]--;
	}
	else {
		if (isaddr32)
			regs.longregs[regedi]++;
		else
			regs.wordregs[regdi]++;
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if ((reptype == 1) && !zf) {
		return;
	}
	else if ((reptype == 2) && zf) {
		return;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* AF SCASW */
void op_AF() {
	if (isaddr32) {
		if (reptype && (regs.longregs[regecx] == 0)) {
			return;
		}
	}
	else {
		if (reptype && (regs.wordregs[regcx] == 0)) {
			return;
		}
	}

	if (isoper32) {
		if (isaddr32) {
			oper1_32 = regs.longregs[regeax];
			oper2_32 = getmem32(segcache[reges], regs.longregs[regedi]);
			flag_sub32(oper1_32, oper2_32);
		}
		else {
			oper1_32 = regs.longregs[regeax];
			oper2_32 = getmem32(segcache[reges], regs.wordregs[regdi]);
			flag_sub32(oper1_32, oper2_32);
		}
	}
	else {
		if (isaddr32) {
			oper1 = regs.wordregs[regax];
			oper2 = getmem16(segcache[reges], regs.longregs[regedi]);
			flag_sub16(oper1, oper2);
		}
		else {
			oper1 = regs.wordregs[regax];
			oper2 = getmem16(segcache[reges], regs.wordregs[regdi]);
			flag_sub16(oper1, oper2);
		}
	}
	if (df) {
		if (isaddr32)
			regs.longregs[regedi] -= isoper32 ? 4 : 2;
		else
			regs.wordregs[regdi] -= isoper32 ? 4 : 2;
	}
	else {
		if (isaddr32)
			regs.longregs[regedi] += isoper32 ? 4 : 2;
		else
			regs.wordregs[regdi] += isoper32 ? 4 : 2;
	}

	if (reptype) {
		if (isaddr32)
			regs.longregs[regecx]--;
		else
			regs.wordregs[regcx]--;
	}

	if ((reptype == 1) && !zf) {
		return;
	}
	else if ((reptype == 2) && zf) { //did i fix a typo bug? this used to be & instead of &&
		return;
	}

	if (!reptype) {
		return;
	}

	ip = firstip;
}

/* B0 MOV regs.byteregs[regal] Ib */
void op_B0() {
	regs.byteregs[regal] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B1 MOV regs.byteregs[regcl] Ib */
void op_B1() {
	regs.byteregs[regcl] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B2 MOV regs.byteregs[regdl] Ib */
void op_B2() {
	regs.byteregs[regdl] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B3 MOV regs.byteregs[regbl] Ib */
void op_B3() {
	regs.byteregs[regbl] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B4 MOV regs.byteregs[regah] Ib */
void op_B4() {
	regs.byteregs[regah] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B5 MOV regs.byteregs[regch] Ib */
void op_B5() {
	regs.byteregs[regch] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B6 MOV regs.byteregs[regdh] Ib */
void op_B6() {
	regs.byteregs[regdh] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* B7 MOV regs.byteregs[regbh] Ib */
void op_B7() {
	regs.byteregs[regbh] = getmem8(segcache[regcs], ip);
	StepIP(1);
}

/* MOV eAX Iv */
/* MOV eCX Iv */
/* MOV eDX Iv */
/* MOV eBX Iv */
/* MOV eSP Iv */
/* MOV eBP Iv */
/* MOV eSI Iv */
/* MOV eDI Iv */
void op_mov() {
	if (isoper32) {
		oper1_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		putreg32(opcode & 7, oper1_32);
	}
	else {
		oper1 = getmem16(segcache[regcs], ip);
		StepIP(2);
		putreg16(opcode & 7, oper1);
	}
}

/* C0 GRP2 byte imm8 (80186+) */
void op_C0() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = getmem8(segcache[regcs], ip);
	StepIP(1);
	writerm8(rm, op_grp2_8(oper2b));
}

/* C1 GRP2 word imm8 (80186+) */
void op_C1() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		oper2_32 = getmem8(segcache[regcs], ip);
		writerm32(rm, op_grp2_32((uint8_t)oper2_32));
	}
	else {
		oper1 = readrm16(rm);
		oper2 = getmem8(segcache[regcs], ip);
		writerm16(rm, op_grp2_16((uint8_t)oper2));
	}
	StepIP(1);
}

/* C2 RET Iw */
void op_C2() {
	oper1 = getmem16(segcache[regcs], ip);
	ip = pop();
	if (isaddr32) {
		regs.longregs[regesp] += (uint32_t)oper1;
	}
	else {
		regs.wordregs[regsp] += oper1;
	}
}

/* C3 RET */
void op_C3() {
	ip = pop();
}

/* C4 LES Gv Mp */
void op_C4() {
	modregrm();
	getea(rm);
	if (isoper32) {
		putreg32(reg, cpu_readl(ea) | ((uint32_t)cpu_read(ea + 1) << 8) | ((uint32_t)cpu_read(ea + 2) << 16) | ((uint32_t)cpu_read(ea + 3) << 24));
		putsegreg(reges, cpu_read(ea + 4) | ((uint16_t)cpu_read(ea + 5) << 8));
	}
	else {
		putreg16(reg, cpu_read(ea) + cpu_read(ea + 1) * 256);
		putsegreg(reges, cpu_read(ea + 2) + cpu_read(ea + 3) * 256);
	}
}

/* C5 LDS Gv Mp */
void op_C5() {
	modregrm();
	getea(rm);
	if (isoper32) {
		putreg32(reg, cpu_read(ea) | ((uint32_t)cpu_read(ea + 1) << 8) | ((uint32_t)cpu_read(ea + 2) << 16) | ((uint32_t)cpu_read(ea + 3) << 24));
		putsegreg(regds, cpu_read(ea + 4) | ((uint16_t)cpu_read(ea + 5) << 8));
	}
	else {
		putreg16(reg, cpu_read(ea) | ((uint16_t)cpu_read(ea + 1) << 8));
		putsegreg(regds, cpu_read(ea + 2) | ((uint16_t)cpu_read(ea + 3) << 8));
	}
}

/* C6 MOV Eb Ib */
void op_C6() {
	modregrm();
	writerm8(rm, getmem8(segcache[regcs], ip));
	StepIP(1);
}

/* C7 MOV Ev Iv */
void op_C7() {
	modregrm();
	if (isoper32) {
		writerm32(rm, getmem32(segcache[regcs], ip));
		StepIP(4);
	}
	else {
		writerm16(rm, getmem16(segcache[regcs], ip));
		StepIP(2);
	}
}

/* C8 ENTER (80186+) */
void op_C8() {
	//if (isoper32 || isaddr32) { printf("32-bit op attempt on %02X", opcode); while(1); }
	stacksize = getmem16(segcache[regcs], ip);
	StepIP(2);
	nestlev = getmem8(segcache[regcs], ip);
	StepIP(1);
	if (isoper32) {
		push(regs.longregs[regebp]);
		frametemp32 = regs.longregs[regesp];
		if (nestlev) {
			for (temp16 = 1; temp16 < nestlev; ++temp16) {
				regs.longregs[regebp] -= 4;
				push(regs.longregs[regebp]);
			}

			push(frametemp32); //regs.wordregs[regsp]);
		}

		regs.longregs[regebp] = frametemp32;
		regs.longregs[regesp] = regs.longregs[regebp] - (uint32_t)stacksize;
	}
	else {
		push(regs.wordregs[regbp]);
		frametemp = regs.wordregs[regsp];
		if (nestlev) {
			for (temp16 = 1; temp16 < nestlev; ++temp16) {
				regs.wordregs[regbp] -= 2;
				push(regs.wordregs[regbp]);
			}

			push(frametemp); //regs.wordregs[regsp]);
		}

		regs.wordregs[regbp] = frametemp;
		regs.wordregs[regsp] = regs.wordregs[regbp] - stacksize;
	}
}

/* C9 LEAVE (80186+) */
void op_C9() {
	if (isoper32) {
		//isaddr32 = 1; //TODO: Correct? Force isaddr32 to make pop move SP by 4
		regs.longregs[regesp] = regs.longregs[regebp];
		regs.longregs[regebp] = pop();
	}
	else {
		//isaddr32 = 0; //TODO: Correct? Force isaddr32 to make pop move SP by 2
		regs.wordregs[regsp] = regs.wordregs[regbp];
		regs.wordregs[regbp] = pop();
	}
}

/* CA RETF Iw */
void op_CA() {
	oper1 = getmem16(segcache[regcs], ip);
	cpu_retf(oper1);
	//ip = pop();
	//putsegreg(regcs, pop());
	/*if (isaddr32) {
		regs.longregs[regesp] += (uint32_t)oper1;
	}
	else {
		regs.wordregs[regsp] += oper1;
	}*/
}

/* CB RETF */
void op_CB() {
	cpu_retf(0);
	//ip = pop();
	//putsegreg(regcs, pop());
}

/* CC INT 3 */
void op_CC() {
	cpu_intcall(3, INT_SOURCE_SOFTWARE, 0);
}

/* CD INT Ib */
void op_CD() {
	oper1b = getmem8(segcache[regcs], ip);
	StepIP(1);
	cpu_intcall(oper1b, INT_SOURCE_SOFTWARE, 0);
}

/* CE INTO */
void op_CE() {
	if (of) {
		cpu_intcall(4, INT_SOURCE_SOFTWARE, 0);
	}
}

/* CF IRET */
void op_CF() {
	cpu_iret();
}

/* D0 GRP2 Eb 1 */
void op_D0() {
	modregrm();
	oper1b = readrm8(rm);
	writerm8(rm, op_grp2_8(1));
}

/* D1 GRP2 Ev 1 */
void op_D1() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		writerm32(rm, op_grp2_32(1));
	}
	else {
		oper1 = readrm16(rm);
		writerm16(rm, op_grp2_16(1));
	}
}

/* D2 GRP2 Eb regs.byteregs[regcl] */
void op_D2() {
	modregrm();
	oper1b = readrm8(rm);
	writerm8(rm, op_grp2_8(regs.byteregs[regcl]));
}

/* D3 GRP2 Ev regs.byteregs[regcl] */
void op_D3() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		writerm32(rm, op_grp2_32(regs.byteregs[regcl]));
	}
	else {
		oper1 = readrm16(rm);
		writerm16(rm, op_grp2_16(regs.byteregs[regcl]));
	}
}

/* D4 AAM I0 */
void op_D4() {
	oper1 = getmem8(segcache[regcs], ip);
	StepIP(1);
	if (!oper1) {
		//cpu_intcall(0, INT_SOURCE_EXCEPTION, 0);
		exception(0, 0); //DE
		return;
	}	/* division by zero */

	regs.byteregs[regah] = (regs.byteregs[regal] / oper1) & 255;
	regs.byteregs[regal] = (regs.byteregs[regal] % oper1) & 255;
	flag_szp16(regs.wordregs[regax]);
}

/* D5 AAD I0 */
void op_D5() {
	oper1 = getmem8(segcache[regcs], ip);
	StepIP(1);
	regs.byteregs[regal] = (regs.byteregs[regah] * oper1 + regs.byteregs[regal]) & 255;
	regs.byteregs[regah] = 0;
	flag_szp16(regs.byteregs[regah] * oper1 + regs.byteregs[regal]);
	sf = 0;
}

/* D6 XLAT on 80186+, SALC on 8086/8088 */
/* D7 XLAT */
void op_D6_D7() {
	if (isaddr32) {
		regs.byteregs[regal] = cpu_read(useseg + (uint32_t)regs.longregs[regebx] + (uint32_t)regs.byteregs[regal]);
	}
	else {
		regs.byteregs[regal] = cpu_read(useseg + (uint32_t)regs.wordregs[regbx] + (uint32_t)regs.byteregs[regal]);
	}
}

/* escape to FPU */
void op_fpu() {
	//if (have387) {
	if ((cr[0] & 4) == 0) {
		//fpu_exec();
		/*modregrm();
		if (mode == 3) {
			fpu_reg_op(NULL, 0);
		}
		else {
			getea(rm);
			fpu_mem_op(NULL, ea, currentseg);
		}*/
		OpFpu(opcode);
	}
	else {
		StepIP(1);
		exception(7, 0); //NM (TODO: or is it a UD?)
	}
}

/* E0 LOOPNZ Jb */
void op_E0() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (isaddr32) {
		regs.longregs[regecx]--;
		if ((regs.longregs[regecx]) && !zf) {
			ip = ip + temp32;
		}
	}
	else {
		regs.wordregs[regcx]--;
		if ((regs.wordregs[regcx]) && !zf) {
			ip = ip + temp32;
		}
	}
}

/* E1 LOOPZ Jb */
void op_E1() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (isaddr32) {
		regs.longregs[regecx]--;
		if (regs.longregs[regecx] && (zf == 1)) {
			ip = ip + temp32;
		}
	}
	else {
		regs.wordregs[regcx]--;
		if (regs.wordregs[regcx] && (zf == 1)) {
			ip = ip + temp32;
		}
	}
}

/* E2 LOOP Jb */
void op_E2() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (isaddr32) {
		regs.longregs[regecx]--;
		if (regs.longregs[regecx]) {
			ip = ip + temp32;
		}
	}
	else {
		regs.wordregs[regcx]--;
		if (regs.wordregs[regcx]) {
			ip = ip + temp32;
		}
	}
}

/* E3 JCXZ Jb */
void op_E3() {
	temp32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	if (isaddr32) {
		if (!regs.longregs[regecx]) {
			ip = ip + temp32;
		}
	}
	else {
		if (!regs.wordregs[regcx]) {
			ip = ip + temp32;
		}
	}
}

/* E4 IN regs.byteregs[regal] Ib */
void op_E4() {
	oper1b = getmem8(segcache[regcs], ip);
	StepIP(1);
	regs.byteregs[regal] = (uint8_t)port_read(oper1b);
}

/* E5 IN eAX Ib */
void op_E5() {
	oper1b = getmem8(segcache[regcs], ip);
	StepIP(1);
	if (isoper32) {
		regs.longregs[regeax] = port_readl(oper1b);
	}
	else {
		regs.wordregs[regax] = port_readw(oper1b);
	}
}

/* E6 OUT Ib regs.byteregs[regal] */
void op_E6() {
	oper1b = getmem8(segcache[regcs], ip);
	StepIP(1);
	port_write(oper1b, regs.byteregs[regal]);
}

/* E7 OUT Ib eAX */
void op_E7() {
	oper1b = getmem8(segcache[regcs], ip);
	StepIP(1);
	if (isoper32) {
		port_writel(oper1b, regs.longregs[regeax]);
	}
	else {
		port_writew(oper1b, regs.wordregs[regax]);
	}
}

/* E8 CALL Jv */
void op_E8() {
	if (isoper32) {
		oper1_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		push(ip);
	}
	else {
		oper1_32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
		push(ip);
	}
	ip = ip + oper1_32;
}

/* E9 JMP Jv */
void op_E9() {
	if (isoper32) {
		oper1_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
	}
	else {
		oper1_32 = (int32_t)(int16_t)getmem16(segcache[regcs], ip);
		StepIP(2);
	}
	ip = ip + oper1_32;
}

/* EA JMP Ap */
void op_EA() {
	if (isoper32) {
		oper1_32 = getmem32(segcache[regcs], ip);
		StepIP(4);
		oper2 = getmem16(segcache[regcs], ip);
		ip = oper1_32;
		putsegreg(regcs, oper2);
		//debug_log(DEBUG_DETAIL, "32-bit far jump CS = %04X, IP = %08X", oper2, oper1_32);
	}
	else {
		oper1 = getmem16(segcache[regcs], ip);
		StepIP(2);
		oper2 = getmem16(segcache[regcs], ip);
		ip = oper1;
		putsegreg(regcs, oper2);
		//debug_log(DEBUG_DETAIL, "16-bit far jump CS = %04X, IP = %04X", oper2, oper1);
	}
}

/* EB JMP Jb */
void op_EB() {
	oper1_32 = signext8to32(getmem8(segcache[regcs], ip));
	StepIP(1);
	ip = ip + oper1_32;
}

/* EC IN regs.byteregs[regal] regdx */
void op_EC() {
	oper1 = regs.wordregs[regdx];
	regs.byteregs[regal] = (uint8_t)port_read(oper1);
}

/* ED IN eAX regdx */
void op_ED() {
	oper1 = regs.wordregs[regdx];
	if (isoper32) {
		regs.longregs[regeax] = port_readl(oper1);
	}
	else {
		regs.wordregs[regax] = port_readw(oper1);
	}
}

/* EE OUT regdx regs.byteregs[regal] */
void op_EE() {
	oper1 = regs.wordregs[regdx];
	port_write(oper1, regs.byteregs[regal]);
}

/* EF OUT regdx eAX */
void op_EF() {
	oper1 = regs.wordregs[regdx];
	if (isoper32) {
		port_writel(oper1, regs.longregs[regeax]);
	}
	else {
		port_writew(oper1, regs.wordregs[regax]);
	}
}

/* CC INT 1 */
void op_F1() {
	cpu_intcall(1, INT_SOURCE_SOFTWARE, 0);
}

/* F4 HLT */
void op_F4() {
	hltstate = 1;
}

/* F5 CMC */
void op_F5() {
	if (!cf) {
		cf = 1;
	}
	else {
		cf = 0;
	}
}

/* F6 GRP3a Eb */
void op_F6() {
	modregrm();
	oper1b = readrm8(rm);
	op_grp3_8();
	if ((reg > 1) && (reg < 4)) {
		writerm8(rm, res8);
	}
}

/* F7 GRP3b Ev */
void op_F7() {
	modregrm();
	if (isoper32) {
		oper1_32 = readrm32(rm);
		op_grp3_32();
		if ((reg > 1) && (reg < 4)) {
			writerm32(rm, res32);
		}
	}
	else {
		oper1 = readrm16(rm);
		op_grp3_16();
		if ((reg > 1) && (reg < 4)) {
			writerm16(rm, res16);
		}
	}
}

/* F8 CLC */
void op_F8() {
	cf = 0;
}

/* F9 STC */
void op_F9() {
	cf = 1;
}

/* FA CLI */
void op_FA() {
	if (protected) {
		if (iopl >= cpl) {
			ifl = 0;
		}
		else {
			exception(13, 0); //GP(0)
		}
	}
	else {
		ifl = 0;
	}
}

/* FB STI */
void op_FB() {
	if (protected) {
		if (iopl >= cpl) {
			ifl = 1;
		}
		else {
			exception(13, 0); //GP(0)
		}
	}
	else {
		ifl = 1;
	}
}

/* FC CLD */
void op_FC() {
	df = 0;
}

/* FD STD */
void op_FD() {
	df = 1;
}

/* FE GRP4 Eb */
void op_FE() {
	modregrm();
	oper1b = readrm8(rm);
	oper2b = 1;
	if (!reg) {
		tempcf = cf;
		res8 = oper1b + oper2b;
		flag_add8(oper1b, oper2b);
		cf = tempcf;
		writerm8(rm, res8);
	}
	else {
		tempcf = cf;
		res8 = oper1b - oper2b;
		flag_sub8(oper1b, oper2b);
		cf = tempcf;
		writerm8(rm, res8);
	}
}

/* FF GRP5 Ev */
void op_FF() {
	if (isoper32) {
		//if (((getmem8(segcache[regcs], ip) >> 3) & 7) == 6) {
			//special case, if it's a PUSH op, then shadow ESP has to be pre-decremented in case EA is calculated with an ESP base
			//shadow_esp -= 4;
		//}
		modregrm();
		oper1_32 = readrm32(rm);
		op_grp5_32();
		//debug_log(DEBUG_DETAIL, "EA was %08X", ea - useseg);
	}
	else {
		modregrm();
		oper1 = readrm16(rm);
		op_grp5();
	}
}

void op_illegal() {
	ip = firstip;
	exception(6, 0); //UD(0)
	debug_log(DEBUG_INFO, "[CPU] Invalid opcode exception at %04X:%04X (%02X)\r\n", segregs[regcs], firstip, opcode);
}

void (*opcode_table[256])() = {
	op_00, op_01, op_02, op_03, op_04, op_05, op_06, op_07,
	op_08, op_09, op_0A, op_0B, op_0C, op_0D, op_0E, cpu_extop,
	op_10, op_11, op_12, op_13, op_14, op_15, op_16, op_17,
	op_18, op_19, op_1A, op_1B, op_1C, op_1D, op_1E, op_1F,
	op_20, op_21, op_22, op_23, op_24, op_25, op_illegal, op_27,
	op_28, op_29, op_2A, op_2B, op_2C, op_2D, op_illegal, op_2F,
	op_30, op_31, op_32, op_33, op_34, op_35, op_illegal, op_37,
	op_38, op_39, op_3A, op_3B, op_3C, op_3D, op_illegal, op_3F,
	op_inc, op_inc, op_inc, op_inc, op_inc, op_inc, op_inc, op_inc,
	op_dec, op_dec, op_dec, op_dec, op_dec, op_dec, op_dec, op_dec,
	op_push, op_push, op_push, op_push, op_54, op_push, op_push, op_push,
	op_pop, op_pop, op_pop, op_pop, op_pop, op_pop, op_pop, op_pop,
	op_60, op_61, op_62, op_63, op_illegal, op_illegal, op_illegal, op_illegal,
	op_68, op_69, op_6A, op_6B, op_6C, op_6D, op_6E, op_6F,
	op_70, op_71, op_72, op_73, op_74, op_75, op_76, op_77,
	op_78, op_79, op_7A, op_7B, op_7C, op_7D, op_7E, op_7F,
	op_80_82, op_81_83, op_80_82, op_81_83, op_84, op_85, op_86, op_87,
	op_88, op_89, op_8A, op_8B, op_8C, op_8D, op_8E, op_8F,
	op_90, op_xchg, op_xchg, op_xchg, op_xchg, op_xchg, op_xchg, op_xchg,
	op_98, op_99, op_9A, op_9B, op_9C, op_9D, op_9E, op_9F,
	op_A0, op_A1, op_A2, op_A3, op_A4, op_A5, op_A6, op_A7,
	op_A8, op_A9, op_AA, op_AB, op_AC, op_AD, op_AE, op_AF,
	op_B0, op_B1, op_B2, op_B3, op_B4, op_B5, op_B6, op_B7,
	op_mov, op_mov, op_mov, op_mov, op_mov, op_mov, op_mov, op_mov,
	op_C0, op_C1, op_C2, op_C3, op_C4, op_C5, op_C6, op_C7,
	op_C8, op_C9, op_CA, op_CB, op_CC, op_CD, op_CE, op_CF,
	op_D0, op_D1, op_D2, op_D3, op_D4, op_D5, op_D6_D7, op_D6_D7,
	op_fpu, op_fpu, op_fpu, op_fpu, op_fpu, op_fpu, op_fpu, op_fpu,
	op_E0, op_E1, op_E2, op_E3, op_E4, op_E5, op_E6, op_E7,
	op_E8, op_E9, op_EA, op_EB, op_EC, op_ED, op_EE, op_EF,
	op_illegal, op_F1, op_illegal, op_illegal, op_F4, op_F5, op_F6, op_F7,
	op_F8, op_F9, op_FA, op_FB, op_FC, op_FD, op_FE, op_FF,
};

void exec86(uint32_t execloops) {

	uint32_t loopcount;
	uint8_t docontinue;
	uint8_t bufop[12];
	uint8_t regscopy[sizeof(regs)];
	uint8_t segscopy[sizeof(segregs)];
	uint8_t cachecopy[sizeof(segcache)];
	//uint8_t crcopy[512];
	uint32_t flagscopy;

	for (loopcount = 0; loopcount < execloops; loopcount++) {
        if (unlikely(ifl && (i8259_controller.interrupt_request_register & (~i8259_controller.interrupt_mask_register)))) {
            cpu_intcall(nextintr(), INT_SOURCE_HARDWARE, 0); // get next interrupt from the i8259, if any d
        }
		//EXTREME HACK ALERT (saving CPU state in case we need to restore later on an exception)
		memcpy(regscopy, &regs, sizeof(regs));
		memcpy(segscopy, &segregs, sizeof(segregs));
		memcpy(cachecopy, &segcache, sizeof(segcache));
		//memcpy(crcopy, &cr, sizeof(cr));
		flagscopy = makeflagsword();
		//END EXTREME HACK ALERT
		shadow_esp = regs.longregs[regesp];

		doexception = 0;
		exceptionerr = 0;
		nowrite = 0;
		exceptionip = ip;
		startcpl = cpl;

		//if (!cpu_interruptCheck(i8259, 0)) {
		//	cpu_interruptCheck(i8259_slave, 1);
		//}

		if (trap_toggle) {
			// cpu_intcall()
			cpu_intcall(1, INT_SOURCE_HARDWARE, 0);
			// exception(1, 0); //GB
		}
		//else if (ifl) {
			//cpu_interruptCheck(i8259);
		//}

		if (tf) {
			trap_toggle = 1;
		}
		else {
			trap_toggle = 0;
		}

		if (hltstate) goto skipexecution;

		reptype = 0;
		segoverride = 0;
		useseg = segcache[regds];
		currentseg = regds;
		if (segis32[regcs] && !v86f) {
			isaddr32 = 1;
			isoper32 = 1;
		}
		else {
			isaddr32 = 0;
			isoper32 = 0;
		}
		docontinue = 0;
		firstip = segis32[regcs] ? ip : ip & 0xFFFF;
		if (showops) for (uint32_t i = 0; i < 12; i++) {
			bufop[i] = cpu_read(segcache[regcs] + ip + i);
		}

		while (!docontinue) {
			if (!segis32[regcs] || v86f) ip = ip & 0xFFFF;
			savecs = segregs[regcs];
			saveip = ip;
			opcode = getmem8(segcache[regcs], ip);
			StepIP(1);

			switch (opcode) {
				/* segment prefix check */
			case 0x2E:	/* segment segregs[regcs] */
				useseg = segcache[regcs];
				segoverride = 1;
				currentseg = regcs;
				break;

			case 0x3E:	/* segment segregs[regds] */
				useseg = segcache[regds];
				segoverride = 1;
				currentseg = regds;
				break;

			case 0x26:	/* segment segregs[reges] */
				useseg = segcache[reges];
				segoverride = 1;
				currentseg = reges;
				break;

			case 0x36:	/* segment segregs[regss] */
				useseg = segcache[regss];
				segoverride = 1;
				currentseg = regss;
				break;

			case 0x64: /* segment segregs[regfs] */
				useseg = segcache[regfs];
				segoverride = 1;
				currentseg = regfs;
				break;

			case 0x65: /* segment segregs[reggs] */
				useseg = segcache[reggs];
				segoverride = 1;
				currentseg = reggs;
				break;

			case 0x66: /* operand size override */
				isoper32 ^= 1;
				break;

			case 0x67: /* address size override */
				isaddr32 ^= 1;
				break;

			case 0xF0:	/* F0 LOCK */
				break;

				/* repetition prefix check */
			case 0xF3:	/* REP/REPE/REPZ */
				reptype = 1;
				break;

			case 0xF2:	/* REPNE/REPNZ */
				reptype = 2;
				break;

			default:
				docontinue = 1;
				break;
			}
		}

		totalexec++;
printf("[%p] %02x\n", ip, opcode);
		(*opcode_table[opcode])();

		//if (startcpl == 3) showops = 1; else showops = 0;

		if (showops) { // || doexception) {
			uint32_t i;
			//if (isoper32) printf("32-bit operand %02X\n", opcode);
			//if (isaddr32) printf("32-bit addressing %02X\n", opcode);
			if (doexception) debug_log(DEBUG_DETAIL, "Exception %u, CPU state:\n", exceptionval);
			if (!reptype || (reptype && !regs.wordregs[regcx])) {
				//uint8_t buf[32];
				printf("ud\n");
				/*
				ud_t ud;
				ud_init(&ud);
				if (protected && !v86f)
					debug_log(DEBUG_DETAIL, "%08X: ", segcache[regcs] + firstip);
				else
					debug_log(DEBUG_DETAIL, "%04X:%04X ", savecs, firstip);
				for (i = 0; i < 12; i++) {
					debug_log(DEBUG_DETAIL, "%02X ", bufop[i]);
				}
				ud_set_input_buffer(&ud, bufop, 12);
				ud_set_mode(&ud, isoper32 ? 32 : 16);
				ud_set_syntax(&ud, UD_SYN_INTEL);
				ud_disassemble(&ud);
				debug_log(DEBUG_DETAIL, "%s\n", ud_insn_asm(&ud));
				/*if (isoper32)
					debug_log(DEBUG_DETAIL, "oper1_32 = %08X, oper2_32 = %08X\n", oper1_32, oper2_32);
				else
					debug_log(DEBUG_DETAIL, "oper1 = %04X, oper2 = %04X\n", oper1, oper2);* /
				debug_log(DEBUG_DETAIL, "\taddr32: %u\toper32: %u\t TF: %u\tIF: %u\tV86: %u\n", isaddr32, isoper32, tf, ifl, v86f);
				debug_log(DEBUG_DETAIL, "\tEAX: %08X\tECX: %08X\tEDX: %08X\tEBX: %08X\n",
					regs.longregs[regeax], regs.longregs[regecx], regs.longregs[regedx], regs.longregs[regebx]);
				debug_log(DEBUG_DETAIL, "\tESI: %08X\tEDI: %08X\tESP: %08X\tEBP: %08X\n",
					regs.longregs[regesi], regs.longregs[regedi], regs.longregs[regesp], regs.longregs[regebp]);
				debug_log(DEBUG_DETAIL, "\tCS: %04X\tDS: %04X\tES: %04X\tSS: %04X\tFS: %04X\tGS: %04X\n\n",
					segregs[regcs], segregs[regds], segregs[reges], segregs[regss], segregs[regfs], segregs[reggs]);
				//cpu_debug_state();
				//getch();
				*/
			}
		}

		if (doexception) {
			// EXTREME HACK ALERT
			memcpy(&regs, regscopy, sizeof(regs));
			memcpy(&segregs, segscopy, sizeof(segregs));
			memcpy(&segcache, cachecopy, sizeof(segcache));
			//memcpy(&cr, crcopy, sizeof(cr));
			// END EXTREME HACK ALERT
			decodeflagsword(flagscopy);
			wrcache_init(); //there was an exception, so flush memory write cache -- the faulting instruction should not modify anything
			cpu_intcall(exceptionval, INT_SOURCE_EXCEPTION, exceptionerr);
		}

	skipexecution:
		;

		wrcache_flush(); //flush/commit memory write cache
	}

}

void cpu_registerIntCallback(uint8_t interrupt, void (*cb)(uint8_t)) {
	int_callback[interrupt] = cb;
}
