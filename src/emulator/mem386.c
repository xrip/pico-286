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
#include <string.h>
#include <stdlib.h>
#include "cpu.h"
#include "memory.h"
#include "emulator.h"

struct mem_s {
	uint32_t start;
	uint32_t size;
	uint8_t* read;
	uint8_t* write;
	uint8_t(*readcb)(void* udata, uint32_t addr);
	void (*writecb)(void* udata, uint32_t addr, uint8_t value);
	void* udata;
	int used;
} mem[32];

//We use a memory write cache during instruction execution and only commit/flush to
//actual RAM after an instruction completes with no faults
struct writecache_s {
	uint32_t addr;
	uint8_t value;
} wrcache[512]; //512 should be more than enough

uint16_t wrcache_count = 0;

uint8_t __attribute__((aligned (4), section(".psram"))) mem_map_lookup[1 << 20]; //4 KB page lookup for emulator memory ranges

//extern int showops;

#define getmap(addr) mem_map_lookup[(addr) >> 12]

/*FUNC_INLINE int getmap(uint32_t addr32) {
	int i;
	for (i = 0; i < 32; i++) {
		if (mem[i].used) {
			if ((addr32 >= mem[i].start) && (addr32 < (mem[i].start + mem[i].size))) {
				return i;
			}
		}
	}
	return -1;
}*/

void wrcache_init() {
	wrcache_count = 0;
}

void wrcache_flush() {
	uint16_t i;
	for (i = 0; i < wrcache_count; i++) {
		int map = getmap(wrcache[i].addr);
		if (map == 0xFF) {
			continue;
		}

		if (mem[map].write != NULL) {
			mem[map].write[wrcache[i].addr - mem[map].start] = wrcache[i].value;
			continue;
		}

		if (mem[map].writecb != NULL) {
			(*mem[map].writecb)(mem[map].udata, wrcache[i].addr, wrcache[i].value);
			continue;
		}
	}
	wrcache_count = 0;
}

void wrcache_write(uint32_t addr32, uint8_t value) {
	wrcache[wrcache_count].addr = addr32;
	wrcache[wrcache_count].value = value;
	wrcache_count++;
	if (wrcache_count == 512) {
		printf("FATAL: wrcache_count == 512\n");
		while(1);
	}
}

int wrcache_read(uint32_t addr32, uint8_t* dst) {
	uint16_t i;
	for (i = 0; i < wrcache_count; i++) {
		if (wrcache[i].addr == addr32) {
			*dst = wrcache[i].value;
			return 1;
		}
	}
	return 0;
}

uint8_t cpu_read_linear(uint32_t addr32) {
	int map;
	uint8_t cacheval;

	if (!a20_enabled) addr32 &= 0xFFFFF;

	if (wrcache_read(addr32, &cacheval)) {
		return cacheval;
	}

	map = getmap(addr32);
	if (map == 0xFF) {
		return 0xFF;
	}

	if (mem[map].read != NULL) {
		return mem[map].read[addr32 - mem[map].start];
	}

	if (mem[map].readcb != NULL) {
		return (*mem[map].readcb)(mem[map].udata, addr32);
	}

	return 0xFF;
}

FUNC_FORCE_INLINE uint32_t translate_page(uint32_t addr32, int iswrite) {
	uint32_t dir, table, offset, dentry_addr, dentry, tentry_addr, tentry, physical;
	uint8_t* dptr;
	uint8_t* tptr;
	int map, eff_user;

	// Split the 32-bit linear address
	dir = (addr32 >> 22) & 0x3FF;
	table = (addr32 >> 12) & 0x3FF;
	offset = addr32 & 0xFFF;

	// Get the address of the directory entry
	dentry_addr = (cr[3] & 0xFFFFF000) + (dir << 2);
	map = getmap(dentry_addr);
	if (map == 0xFF) {
		debug_log(DEBUG_DETAIL, "PAGE FAULT: %08X -> NOT PRESENT\n", addr32);
		tentry = 0;
		cr[2] = addr32;
		exception(14, (iswrite ? 2 : 0) | ((startcpl == 3) ? 4 : 0)); //PF
		return 0xFFFFFFFF;
	}

	dptr = &mem[map].read[dentry_addr - mem[map].start];
	dentry =
		(uint32_t)dptr[0] |
		((uint32_t)dptr[1] << 8) |
		((uint32_t)dptr[2] << 16) |
		((uint32_t)dptr[3] << 24);

	if ((dentry & 1) == 0) { //not present
		debug_log(DEBUG_DETAIL, "PAGE FAULT: %08X -> NOT PRESENT\n", addr32);
		cr[2] = addr32;
		exception(14, (iswrite ? 2 : 0) | ((startcpl == 3) ? 4 : 0)); //PF
		return 0xFFFFFFFF;
	}

	// Mask out the lower 12 bits (flags) to get the base address of the page table
	tentry_addr = (dentry & 0xFFFFF000) + (table << 2);
	map = getmap(tentry_addr);
	if (map == 0xFF) {
		debug_log(DEBUG_DETAIL, "PAGE FAULT: %08X -> NOT PRESENT\n", addr32);
		tentry = 0;
		cr[2] = addr32;
		exception(14, (iswrite ? 2 : 0) | ((startcpl == 3) ? 4 : 0)); //PF
		return 0xFFFFFFFF;
	}

	tptr = &mem[map].read[tentry_addr - mem[map].start];
	tentry =
		(uint32_t)tptr[0] |
		((uint32_t)tptr[1] << 8) |
		((uint32_t)tptr[2] << 16) |
		((uint32_t)tptr[3] << 24);

	/*if ((startcpl == 3) && ((dentry & 0x4) && (tentry & 0x4))) {
		tptr[0] |= 0x40;  // PTE accessed bit
		cr[2] = addr32;
		exception(14, 1 | 4 | (iswrite ? 2 : 0));
		return 0xFFFFFFFF;
	}*/

	// Mask out the lower 12 bits (flags) to get the base address of the page
	physical = (tentry & 0xFFFFF000) + offset;

	if ((tentry & 1) == 0) { //not present
		debug_log(DEBUG_DETAIL, "PAGE FAULT: map %08X -> NOT PRESENT\n", addr32);
		cr[2] = addr32;
		exception(14, (iswrite ? 2 : 0) | ((startcpl == 3) ? 4 : 0)); //PF
		return 0xFFFFFFFF;
	}

	if ((((tentry & 2) == 0) || ((dentry & 2) == 0)) && iswrite) { //If page not writable
		int canwrite = 0;
		//printf("Not writable check %08X, with CPL = %u\n", linear, cpl);
		if (!(cr[0] & 0x00010000) && (startcpl < 3)) { //as long as WP bit isn't set in CR0, supervisor code can still write
			//printf("supervisor can still write!\n");
			canwrite = 1;
		}
		if (!canwrite) {
			debug_log(DEBUG_DETAIL, "PAGE FAULT: map %08X -> %08X (WP)\n", addr32, physical);
			cr[2] = addr32;
			exception(14, ((startcpl == 3) ? 4 : 0) | 3); //PF
			return 0xFFFFFFFF;
		}
	}

	if (iswrite) {
		//Mark page as dirty if written to
		tptr[0] |= 0x40;
		dptr[0] |= 0x40;
	}
	//dptr[0] |= 0x20;  // PDE accessed bit
	tptr[0] |= 0x20;  // PTE accessed bit

	return physical;
}

void cpu_write_linear(uint32_t addr32, uint8_t value) {
	int map;

	if (nowrite) return;

	if (!a20_enabled) addr32 &= 0xFFFFF; //addr32 &= 0xFFEFFFFF;
	//if (addr32 >= 0x100000) { debug_log(DEBUG_DETAIL, "Write at %08X <- %02X\n", addr32, value); }

	//if (addr32 >= MEMORY_RANGE) return;

	/*if (memory_mapWrite[addr32] != NULL) {
		*(memory_mapWrite[addr32]) = value;
	}
	else if (memory_mapWriteCallback[addr32] != NULL) {
		(*memory_mapWriteCallback[addr32])(memory_udata[addr32], addr32, value);
	}*/

	/*map = getmap(addr32);
	if (map == 0xFF) {
		return;
	}

	if (mem[map].write != NULL) {
		mem[map].write[addr32 - mem[map].start] = value;
		return;
	}

	if (mem[map].writecb != NULL) {
		(*mem[map].writecb)(mem[map].udata, addr32, value);
		return;
	}*/

	wrcache_write(addr32, value);
}

FUNC_INLINE void cpu_write(uint32_t addr32, uint8_t value) {
	int map;

	if (nowrite) return;

	if (cr[0] & 0x80000000) {
		addr32 = translate_page(addr32, 1);
		if (addr32 == 0xFFFFFFFF) return;
	}

	if (!a20_enabled) addr32 &= 0xFFFFF; //addr32 &= 0xFFEFFFFF;
	//if (addr32 >= 0x100000) { debug_log(DEBUG_DETAIL, "Write at %08X <- %02X\n", addr32, value); }

	//if (addr32 >= MEMORY_RANGE) return;

	/*if (memory_mapWrite[addr32] != NULL) {
		*(memory_mapWrite[addr32]) = value;
	}
	else if (memory_mapWriteCallback[addr32] != NULL) {
		(*memory_mapWriteCallback[addr32])(memory_udata[addr32], addr32, value);
	}*/

	/*map = getmap(addr32);
	if (map == 0xFF) {
		return;
	}

	if (mem[map].write != NULL) {
		mem[map].write[addr32 - mem[map].start] = value;
		return;
	}

	if (mem[map].writecb != NULL) {
		(*mem[map].writecb)(mem[map].udata, addr32, value);
		return;
	}*/

	wrcache_write(addr32, value);
}

FUNC_INLINE uint8_t cpu_read(uint32_t addr32) {
	int map;
	uint8_t cacheval;

	if (cr[0] & 0x80000000) {
		addr32 = translate_page(addr32, 0);
		if (addr32 == 0xFFFFFFFF) return 0xFF;
	}

	if (!a20_enabled) addr32 &= 0xFFFFF;

	if (wrcache_read(addr32, &cacheval)) {
		return cacheval;
	}

	map = getmap(addr32);
	if (map == 0xFF) {
		return 0xFF;
	}

	if (mem[map].read != NULL) {
		return mem[map].read[addr32 - mem[map].start];
	}

	if (mem[map].readcb != NULL) {
		return (*mem[map].readcb)(mem[map].udata, addr32);
	}

	return 0xFF;
}

FUNC_INLINE void cpu_writew(uint32_t addr32, uint16_t value) {
	cpu_write(addr32, (uint8_t)value);
	cpu_write(addr32 + 1, (uint8_t)(value >> 8));
}

FUNC_INLINE void cpu_writel(uint32_t addr32, uint32_t value) {
	cpu_write(addr32, (uint8_t)value);
	cpu_write(addr32 + 1, (uint8_t)(value >> 8));
	cpu_write(addr32 + 2, (uint8_t)(value >> 16));
	cpu_write(addr32 + 3, (uint8_t)(value >> 24));
}

void cpu_writew_linear(uint32_t addr32, uint16_t value) {
	cpu_write_linear(addr32, (uint8_t)value);
	cpu_write_linear(addr32 + 1, (uint8_t)(value >> 8));
}

void cpu_writel_linear(uint32_t addr32, uint32_t value) {
	cpu_write_linear(addr32, (uint8_t)value);
	cpu_write_linear(addr32 + 1, (uint8_t)(value >> 8));
	cpu_write_linear(addr32 + 2, (uint8_t)(value >> 16));
	cpu_write_linear(addr32 + 3, (uint8_t)(value >> 24));
}

FUNC_INLINE uint16_t cpu_readw(uint32_t addr32) {
	return ((uint16_t)cpu_read(addr32) | (uint16_t)(cpu_read(addr32 + 1) << 8));
}

FUNC_INLINE uint32_t cpu_readl(uint32_t addr32) {
	return ((uint32_t)cpu_read(addr32) | (uint32_t)(cpu_read(addr32 + 1) << 8) | (uint32_t)(cpu_read(addr32 + 2) << 16) | (uint32_t)(cpu_read(addr32 + 3) << 24));
}

uint16_t cpu_readw_linear(uint32_t addr32) {
	return ((uint16_t)cpu_read_linear(addr32) | (uint16_t)(cpu_read_linear(addr32 + 1) << 8));
}

uint32_t cpu_readl_linear(uint32_t addr32) {
	return ((uint32_t)cpu_read_linear(addr32) | (uint32_t)(cpu_read_linear(addr32 + 1) << 8) | (uint32_t)(cpu_read_linear(addr32 + 2) << 16) | (uint32_t)(cpu_read_linear(addr32 + 3) << 24));
}

void memory_mapRegister(uint32_t start, uint32_t len, uint8_t* readb, uint8_t* writeb) {
	uint8_t i;
	uint32_t j;
	for (i = 0; i < 32; i++) {
		if (mem[i].used == 0) break;
	}
	if (i == 32) {
		debug_log(DEBUG_ERROR, "[MEMORY] Out of memory map structs!\n");
		exit(0);
	}
	mem[i].read = readb;
	mem[i].write = writeb;
	mem[i].readcb = NULL;
	mem[i].writecb = NULL;
	mem[i].start = start;
	mem[i].size = len;
	mem[i].used = 1;

	start >>= 12;
	len >>= 12;
	for (j = start; j < (start + len); j++) {
		mem_map_lookup[j] = i;
	}
}

void memory_mapCallbackRegister(uint32_t start, uint32_t count, uint8_t(*readb)(void*, uint32_t), void (*writeb)(void*, uint32_t, uint8_t), void* udata) {
	uint8_t i;
	uint32_t j;
	for (i = 0; i < 32; i++) {
		if (mem[i].used == 0) break;
	}
	if (i == 32) {
		debug_log(DEBUG_ERROR, "[MEMORY] Out of memory map structs!\n");
		exit(0);
	}
	mem[i].readcb = readb;
	mem[i].writecb = writeb;
	mem[i].read = NULL;
	mem[i].write = NULL;
	mem[i].start = start;
	mem[i].size = count;
	mem[i].udata = udata;
	mem[i].used = 1;

	start >>= 12;
	count >>= 12;
	for (j = start; j < (start + count); j++) {
		mem_map_lookup[j] = i;
	}
}

int memory_init() {
	uint32_t i;

	for (i = 0; i < 32; i++) {
		mem[i].read = NULL;
		mem[i].readcb = NULL;
		mem[i].write = NULL;
		mem[i].writecb = NULL;
		mem[i].used = 0;
	}

	memset(mem_map_lookup, 0xFF, sizeof(mem_map_lookup));

	return 0;
}
