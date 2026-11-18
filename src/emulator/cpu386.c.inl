// 386 instructions whose meaning depends on the operand-size (66h) or address-size (67h)
// prefix, from the Intel 80386 Programmer's Reference Manual, chapter 17.
// Included by cpu.c before exec86().
// exec386() is only called for an instruction with a 66h or 67h prefix; it returns false
// for an opcode it does not handle, which then runs in the 16-bit switch of exec86().
#pragma once

static __not_in_flash() uint8_t fetch8(void) {
    const uint8_t value = getmem8(CS_BASE, CPU_IP);
    StepIP(1);
    return value;
}

static __not_in_flash() uint16_t fetch16(void) {
    const uint16_t value = getmem16(CS_BASE, CPU_IP);
    StepIP(2);
    return value;
}

static __not_in_flash() uint32_t fetch32(void) {
    const uint32_t value = mem_read32(CS_BASE + CPU_IP);
    StepIP(4);
    return value;
}

static __not_in_flash() uint32_t readrm32(uint8_t rmval) {
    if (mode < 3) {
        getea(rmval);
        return mem_read32(ea);
    }
    return dwordregs[rmval];
}

static __not_in_flash() void writerm32(uint8_t rmval, uint32_t value) {
    if (mode < 3) {
        getea(rmval);
        mem_write32(ea, value);
    } else {
        dwordregs[rmval] = value;
    }
}

static __not_in_flash() void push32(uint32_t value) {
    stack_add(-4);
    mem_write32(SS_BASE + stack_top(), value);
}

static __not_in_flash() uint32_t pop32(void) {
    const uint32_t value = mem_read32(SS_BASE + stack_top());
    stack_add(4);
    return value;
}

static __not_in_flash() void flag_szp32(uint32_t value) {
    zf = value == 0;
    sf = value >> 31;
    pf = parity[value & 255];
}

// The ALU group by its 386 number: 0 ADD, 1 OR, 2 ADC, 3 SBB, 4 AND, 5 SUB, 6 XOR, 7 CMP.
static __not_in_flash() uint32_t alu32(uint8_t operation, uint32_t a, uint32_t b) {
    uint64_t result;
    switch (operation) {
        case 0: add_flags(32, a, b); return a + b;
        case 5: case 7: sub_flags(32, a, b); return a - b;
        case 1: logic_flags(32, a | b); return a | b;
        case 4: logic_flags(32, a & b); return a & b;
        case 6: logic_flags(32, a ^ b); return a ^ b;
        case 2: // ADC
            result = (uint64_t) a + b + cf;
            cf = (result >> 32) & 1;
            of = (((result ^ a) & (result ^ b)) >> 31) & 1;
            af = ((a ^ b ^ result) >> 4) & 1;
            flag_szp32((uint32_t) result);
            return (uint32_t) result;
        default: // 3 SBB
            result = (uint64_t) a - b - cf;
            cf = (result >> 32) & 1;
            of = (((a ^ b) & (a ^ result)) >> 31) & 1;
            af = ((a ^ b ^ result) >> 4) & 1;
            flag_szp32((uint32_t) result);
            return (uint32_t) result;
    }
}

// Shift and rotate group (C1, D1, D3 /reg) on 32 bits. The count is masked to 5 bits.
// OF comes from the last one-bit step, as on the 386 for every count.
static __not_in_flash() uint32_t shift32(uint32_t value, uint8_t count) {
    count &= 31;
    if (!count) return value;
    uint32_t result = value;
    switch (reg) {
        case 0: // ROL
            result = value << count | value >> (32 - count);
            cf = result & 1;
            of = (result >> 31) ^ cf;
            break;
        case 1: // ROR
            result = value >> count | value << (32 - count);
            cf = result >> 31;
            of = ((result >> 31) ^ (result >> 30)) & 1;
            break;
        case 2: // RCL: 33-bit rotate through CF
            for (uint8_t i = 0; i < count; i++) {
                const uint8_t out = result >> 31;
                result = result << 1 | cf;
                cf = out;
            }
            of = (result >> 31) ^ cf;
            break;
        case 3: // RCR
            for (uint8_t i = 0; i < count; i++) {
                const uint8_t out = result & 1;
                result = result >> 1 | (uint32_t) cf << 31;
                cf = out;
            }
            of = ((result >> 31) ^ (result >> 30)) & 1;
            break;
        case 4:
        case 6: // SHL
            result = value << count;
            cf = (value >> (32 - count)) & 1;
            of = (result >> 31) ^ cf;
            flag_szp32(result);
            break;
        case 5: // SHR
            result = value >> count;
            cf = (value >> (count - 1)) & 1;
            of = count == 1 && (value >> 31);
            flag_szp32(result);
            break;
        case 7: // SAR
            result = (uint32_t) ((int32_t) value >> count);
            cf = ((int32_t) value >> (count - 1)) & 1;
            of = 0;
            flag_szp32(result);
            break;
    }
    return result;
}

// F7 group on 32 bits: TEST, NOT, NEG, MUL, IMUL, DIV, IDIV. EDX:EAX holds the wide value.
static __not_in_flash() void group3_32(void) {
    const uint32_t value = readrm32(rm);
    switch (reg) {
        case 0:
        case 1: // TEST
            alu32(4, value, fetch32());
            break;
        case 2: // NOT
            writerm32(rm, ~value);
            break;
        case 3: // NEG
            writerm32(rm, alu32(5, 0, value));
            cf = value != 0;
            break;
        case 4: { // MUL
            const uint64_t product = (uint64_t) dwordregs[regax] * value;
            dwordregs[regax] = (uint32_t) product;
            dwordregs[regdx] = (uint32_t) (product >> 32);
            cf = of = dwordregs[regdx] != 0;
            break;
        }
        case 5: { // IMUL
            const int64_t product = (int64_t) (int32_t) dwordregs[regax] * (int32_t) value;
            dwordregs[regax] = (uint32_t) product;
            dwordregs[regdx] = (uint32_t) ((uint64_t) product >> 32);
            cf = of = product != (int32_t) product;
            break;
        }
        case 6: { // DIV
            const uint64_t dividend = (uint64_t) dwordregs[regdx] << 32 | dwordregs[regax];
            if (value == 0 || dividend / value > 0xFFFFFFFFu) {
                cpu_fault(0);
                return;
            }
            dwordregs[regax] = (uint32_t) (dividend / value);
            dwordregs[regdx] = (uint32_t) (dividend % value);
            break;
        }
        case 7: { // IDIV
            const int64_t dividend = (int64_t) ((uint64_t) dwordregs[regdx] << 32 | dwordregs[regax]);
            const int32_t divisor = (int32_t) value;
            if (divisor == 0 || (dividend == INT64_MIN && divisor == -1)) {
                cpu_fault(0);
                return;
            }
            const int64_t quotient = dividend / divisor;
            if (quotient != (int32_t) quotient) {
                cpu_fault(0);
                return;
            }
            dwordregs[regax] = (uint32_t) quotient;
            dwordregs[regdx] = (uint32_t) (dividend % divisor);
            break;
        }
    }
}

// Two- and three-operand IMUL on 32 bits.
static __not_in_flash() uint32_t imul32(uint32_t a, uint32_t b) {
    const int64_t product = (int64_t) (int32_t) a * (int32_t) b;
    cf = of = product != (int32_t) product;
    return (uint32_t) product;
}

static __not_in_flash() uint32_t read_sized(uint32_t address, uint8_t size) {
    return size == 1 ? mem_read8(address) : size == 2 ? mem_read16(address) : mem_read32(address);
}

static __not_in_flash() void write_sized(uint32_t address, uint32_t value, uint8_t size) {
    if (size == 1) mem_write8(address, (uint8_t) value);
    else if (size == 2) mem_write16(address, (uint16_t) value);
    else mem_write32(address, value);
}

static __not_in_flash() uint32_t accumulator(uint8_t size) {
    return size == 1 ? CPU_AL : size == 2 ? CPU_AX : dwordregs[regax];
}

static __not_in_flash() void set_accumulator(uint32_t value, uint8_t size) {
    if (size == 1) CPU_AL = (uint8_t) value;
    else if (size == 2) CPU_AX = (uint16_t) value;
    else dwordregs[regax] = value;
}

// CMP flags for 8, 16 or 32 bits.
static __not_in_flash() void compare_sized(uint32_t a, uint32_t b, uint8_t size) {
    if (size == 1) sub_flags(8, (uint8_t) a, (uint8_t) b);
    else if (size == 2) sub_flags(16, (uint16_t) a, (uint16_t) b);
    else alu32(7, a, b);
}

// SI, DI and CX are 32-bit (ESI, EDI, ECX) with the address-size prefix.
static __not_in_flash() uint32_t address_register(uint8_t id) {
    return addrsize32 ? dwordregs[id] : wordregs[id << 1];
}

static __not_in_flash() void add_address_register(uint8_t id, int32_t delta) {
    if (addrsize32) dwordregs[id] += delta;
    else wordregs[id << 1] += delta;
}

static void check_io_permission(uint16_t port, uint8_t size); // protected.c.inl

// I/O in protected mode with CPL > IOPL, and all I/O in V86 mode, goes through the TSS bitmap
static __not_in_flash() uint32_t port_read_sized(uint16_t port, uint8_t size) {
    if (unlikely(cpl > iopl) || unlikely(v86)) check_io_permission(port, size);
    if (size == 1) return (uint8_t) portin(port);
    if (size == 2) return portin16(port);
    return portin16(port) | (uint32_t) portin16(port + 2) << 16;
}

static __not_in_flash() void port_write_sized(uint16_t port, uint32_t value, uint8_t size) {
    if (unlikely(cpl > iopl) || unlikely(v86)) check_io_permission(port, size);
    if (size == 1) portout(port, (uint8_t) value);
    else if (size == 2) portout16(port, (uint16_t) value);
    else {
        portout16(port, (uint16_t) value);
        portout16(port + 2, (uint16_t) (value >> 16));
    }
}

// One step of a string instruction (6C-6F, A4-A7, AA-AF). With REP the instruction runs
// again from its first prefix until the count ends, as in the 16-bit switch.
static __not_in_flash() void string_step(uint8_t opcode, uint8_t size) {
    if (reptype && address_register(regcx) == 0) return;
    const int32_t delta = df ? -(int32_t) size : size;
    const uint32_t source = useseg + address_register(regsi);
    const uint32_t destination = ES_BASE + address_register(regdi);
    bool compare = false;
    switch (opcode & 0xFE) {
        case 0x6C: // INS
            write_sized(destination, port_read_sized(CPU_DX, size), size);
            add_address_register(regdi, delta);
            break;
        case 0x6E: // OUTS
            port_write_sized(CPU_DX, read_sized(source, size), size);
            add_address_register(regsi, delta);
            break;
        case 0xA4: // MOVS
            write_sized(destination, read_sized(source, size), size);
            add_address_register(regsi, delta);
            add_address_register(regdi, delta);
            break;
        case 0xA6: // CMPS
            compare_sized(read_sized(source, size), read_sized(destination, size), size);
            add_address_register(regsi, delta);
            add_address_register(regdi, delta);
            compare = true;
            break;
        case 0xAA: // STOS
            write_sized(destination, accumulator(size), size);
            add_address_register(regdi, delta);
            break;
        case 0xAC: // LODS
            set_accumulator(read_sized(source, size), size);
            add_address_register(regsi, delta);
            break;
        case 0xAE: // SCAS
            compare_sized(accumulator(size), read_sized(destination, size), size);
            add_address_register(regdi, delta);
            compare = true;
            break;
    }
    if (!reptype) return;
    add_address_register(regcx, -1);
    // REPE stops when ZF is clear, REPNE when it is set
    if (compare && (reptype == 1 ? !get_zf() : get_zf())) return;
    ip = firstip;
}

// Word or dword register and r/m operand, by the operand size.
static __not_in_flash() uint32_t readrm_sized(uint8_t rmval) { return opsize32 ? readrm32(rmval) : readrm16(rmval); }
static __not_in_flash() void writerm_sized(uint8_t rmval, uint32_t value) {
    if (opsize32) writerm32(rmval, value);
    else writerm16(rmval, (uint16_t) value);
}
static __not_in_flash() uint32_t getreg_sized(uint8_t id) { return opsize32 ? dwordregs[id] : getreg16(id); }
static __not_in_flash() void putreg_sized(uint8_t id, uint32_t value) {
    if (opsize32) dwordregs[id] = value;
    else putreg16(id, (uint16_t) value);
}

static __not_in_flash() void flag_szp_sized(uint32_t value) {
    if (opsize32) flag_szp32(value);
    else flag_szp16((uint16_t) value);
}

// BT, BTS, BTR, BTC (reg 4-7 of the 0F BA group): CF gets the bit, then the bit is changed.
// A register bit offset can reach outside the operand in memory; an immediate one cannot.
static __not_in_flash() void bit_test(uint8_t operation, uint32_t offset, bool immediate) {
    const uint8_t bits = opsize32 ? 32 : 16;
    uint32_t value, address = 0;
    if (mode == 3) {
        value = getreg_sized(rm);
        offset &= bits - 1;
    } else {
        getea(rm);
        if (immediate) {
            offset &= bits - 1;
        } else {
            const int32_t signed_offset = opsize32 ? (int32_t) offset : (int16_t) offset;
            uint32_t operand_offset = ea - useseg + (uint32_t) ((signed_offset >> (opsize32 ? 5 : 4)) * (bits / 8));
            if (!addrsize32) operand_offset &= 0xFFFF; // 16-bit addresses wrap in the segment
            ea = useseg + operand_offset;
            offset &= bits - 1;
        }
        address = ea;
        value = opsize32 ? mem_read32(address) : mem_read16(address);
    }
    cf = (value >> offset) & 1;
    switch (operation & 3) {
        case 0: return; // BT
        case 1: value |= 1u << offset; break; // BTS
        case 2: value &= ~(1u << offset); break; // BTR
        case 3: value ^= 1u << offset; break; // BTC
    }
    if (mode == 3) putreg_sized(rm, value);
    else if (opsize32) mem_write32(address, value);
    else mem_write16(address, (uint16_t) value);
}

// SHLD and SHRD: shift r/m, filling from a register. The count is masked to 5 bits.
// A 16-bit operand shifts in a 48-bit window filled again with the source
// (SHLD dest:src:src, SHRD src:src:dest), so counts 17-31 work as on the 386.
static __not_in_flash() void double_shift(bool left, uint8_t count) {
    count &= 31;
    const uint32_t destination = readrm_sized(rm);
    if (!count) return;
    const uint8_t bits = opsize32 ? 32 : 16;
    const uint64_t source = getreg_sized(reg);
    uint32_t result;
    if (left) {
        const uint64_t window = opsize32 ? (uint64_t) destination << 32 | source
                                         : (uint64_t) destination << 32 | source << 16 | source;
        const uint8_t window_bits = opsize32 ? 64 : 48;
        result = (uint32_t) ((window << count) >> (window_bits - bits)) & (opsize32 ? 0xFFFFFFFFu : 0xFFFF);
        cf = (window >> (window_bits - count)) & 1;
    } else {
        const uint64_t window = opsize32 ? source << 32 | destination
                                         : source << 32 | source << 16 | destination;
        result = (uint32_t) (window >> count) & (opsize32 ? 0xFFFFFFFFu : 0xFFFF);
        cf = (window >> (count - 1)) & 1;
    }
    of = ((result ^ destination) >> (bits - 1)) & 1;
    flag_szp_sized(result);
    writerm_sized(rm, result);
}

// 386 two-byte opcodes. Returns false for an opcode that is not one of them.
static __not_in_flash() __attribute__((noinline)) bool op_0f_386(uint8_t opcode) {
    if (opcode >= 0x80 && opcode <= 0x8F) { // Jcc rel16/rel32
        const int32_t displacement = opsize32 ? (int32_t) fetch32() : (int16_t) fetch16();
        if (condition(opcode)) jump_relative(displacement);
        return true;
    }
    if (opcode >= 0x90 && opcode <= 0x9F) { // SETcc r/m8
        modregrm();
        writerm8(rm, condition(opcode));
        return true;
    }
    switch (opcode) {
        case 0xA0: case 0xA8: { // PUSH FS / GS
            const uint16_t selector = segregs[opcode == 0xA0 ? regfs : reggs];
            if (opsize32) push32(selector);
            else push(selector);
            return true;
        }
        case 0xA1: case 0xA9: // POP FS / GS
            load_segment(opcode == 0xA1 ? regfs : reggs, opsize32 ? (uint16_t) pop32() : pop());
            return true;
        case 0xA3: case 0xAB: case 0xB3: case 0xBB: // BT, BTS, BTR, BTC r/m, reg
            modregrm();
            bit_test(opcode >> 3, getreg_sized(reg), false);
            return true;
        case 0xBA: // BT group, imm8
            modregrm();
            if (reg < 4) return false;
            if (mode < 3) getea(rm); // the displacement comes before the immediate
            bit_test(reg, fetch8(), true);
            return true;
        case 0xA4: case 0xAC: // SHLD / SHRD r/m, reg, imm8
            modregrm();
            if (mode < 3) getea(rm);
            double_shift(opcode == 0xA4, fetch8());
            return true;
        case 0xA5: case 0xAD: // SHLD / SHRD r/m, reg, CL
            modregrm();
            double_shift(opcode == 0xA5, CPU_CL);
            return true;
        case 0xAF: { // IMUL reg, r/m
            modregrm();
            const uint32_t value = readrm_sized(rm);
            if (opsize32) {
                dwordregs[reg] = imul32(dwordregs[reg], value);
            } else {
                const int32_t product = (int16_t) getreg16(reg) * (int16_t) value;
                cf = of = product != (int16_t) product;
                putreg16(reg, (uint16_t) product);
            }
            return true;
        }
        case 0xB2: case 0xB4: case 0xB5: { // LSS, LFS, LGS
            modregrm();
            if (mode == 3) {
                cpu_fault(6);
                return true;
            }
            getea(rm);
            const uint32_t offset = opsize32 ? mem_read32(ea) : mem_read16(ea);
            const uint16_t selector = mem_read16(ea + (opsize32 ? 4 : 2));
            putreg_sized(reg, offset);
            load_segment(opcode == 0xB2 ? regss : opcode == 0xB4 ? regfs : reggs, selector);
            return true;
        }
        case 0xB6: modregrm(); putreg_sized(reg, readrm8(rm)); return true; // MOVZX r, r/m8
        case 0xB7: modregrm(); putreg_sized(reg, readrm16(rm)); return true; // MOVZX r, r/m16
        case 0xBE: modregrm(); putreg_sized(reg, (uint32_t) (int32_t) (int8_t) readrm8(rm)); return true; // MOVSX
        case 0xBF: modregrm(); putreg_sized(reg, (uint32_t) (int32_t) (int16_t) readrm16(rm)); return true;
        case 0xBC: case 0xBD: { // BSF, BSR: ZF is set and the register kept when the source is 0
            modregrm();
            const uint32_t value = readrm_sized(rm);
            zf = value == 0;
            if (value) putreg_sized(reg, opcode == 0xBC ? (uint32_t) __builtin_ctz(value) : 31u - __builtin_clz(value));
            return true;
        }
    }
    return false;
}

static __not_in_flash() bool exec386(uint8_t opcode) {
    const uint8_t size = opsize32 ? 4 : 2; // operand size of a word instruction

    // String instructions use both prefixes
    if ((opcode >= 0x6C && opcode <= 0x6F) || (opcode >= 0xA4 && opcode <= 0xA7) || (opcode >= 0xAA && opcode <= 0xAF)) {
        string_step(opcode, opcode & 1 ? size : 1);
        return true;
    }

    // Address size alone
    switch (opcode) {
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: { // MOV accumulator to or from moffs
            const uint32_t address = useseg + (addrsize32 ? fetch32() : fetch16());
            const uint8_t width = opcode & 1 ? size : 1;
            if (opcode & 2) write_sized(address, accumulator(width), width);
            else set_accumulator(read_sized(address, width), width);
            return true;
        }
        case 0xD7: // XLAT
            CPU_AL = mem_read8(useseg + (addrsize32 ? dwordregs[regbx] + CPU_AL : (uint16_t) (CPU_BX + CPU_AL)));
            return true;
        case 0xE0: case 0xE1: case 0xE2: { // LOOPNE, LOOPE, LOOP
            const int8_t displacement = (int8_t) fetch8();
            add_address_register(regcx, -1);
            if (address_register(regcx) && (opcode == 0xE2 || get_zf() == (opcode == 0xE1))) jump_relative(displacement);
            return true;
        }
        case 0xE3: { // JCXZ / JECXZ
            const int8_t displacement = (int8_t) fetch8();
            if (!address_register(regcx)) jump_relative(displacement);
            return true;
        }
    }
    if (!opsize32) return false;

    // ALU Ev,Gv / Gv,Ev / eAX,Iv
    if (opcode < 0x40 && (opcode & 1) && (opcode & 7) <= 5) {
        const uint8_t operation = opcode >> 3;
        uint32_t result;
        switch (opcode & 7) {
            case 1:
                modregrm();
                result = alu32(operation, readrm32(rm), dwordregs[reg]);
                if (operation != 7) writerm32(rm, result);
                break;
            case 3:
                modregrm();
                result = alu32(operation, dwordregs[reg], readrm32(rm));
                if (operation != 7) dwordregs[reg] = result;
                break;
            default:
                result = alu32(operation, dwordregs[regax], fetch32());
                if (operation != 7) dwordregs[regax] = result;
                break;
        }
        return true;
    }

    switch (opcode) {
        case 0x06: push32(CPU_ES); return true;
        case 0x0E: push32(CPU_CS); return true;
        case 0x16: push32(CPU_SS); return true;
        case 0x1E: push32(CPU_DS); return true;
        case 0x07: load_segment(reges, (uint16_t) pop32()); return true;
        case 0x17: load_segment(regss, (uint16_t) pop32()); return true;
        case 0x1F: load_segment(regds, (uint16_t) pop32()); return true;

        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            dwordregs[opcode & 7] = inc_dec(dwordregs[opcode & 7], false, 32);
            return true;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            dwordregs[opcode & 7] = inc_dec(dwordregs[opcode & 7], true, 32);
            return true;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            push32(dwordregs[opcode & 7]); // PUSH ESP pushes the value before the push
            return true;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            dwordregs[opcode & 7] = pop32(); // POP ESP: the popped value wins over the SP step
            return true;
        case 0x60: { // PUSHAD
            const uint32_t original_esp = dwordregs[regsp];
            for (uint8_t id = regax; id <= regdi; id++) push32(id == regsp ? original_esp : dwordregs[id]);
            return true;
        }
        case 0x61: { // POPAD. With a 16-bit stack the 386 keeps the high half of the ESP slot.
            uint32_t esp_high = 0;
            for (int id = regdi; id >= regax; id--) {
                const uint32_t value = pop32();
                if (id == regsp) esp_high = value & 0xFFFF0000u;
                else dwordregs[id] = value;
            }
            if (!stack32) dwordregs[regsp] = esp_high | CPU_SP;
            return true;
        }
        case 0x62: { // BOUND r32, m32&32
            modregrm();
            if (mode == 3) {
                cpu_fault(6);
                return true;
            }
            getea(rm);
            const int32_t index = (int32_t) dwordregs[reg];
            if (index < (int32_t) mem_read32(ea) || index > (int32_t) mem_read32(ea + 4)) cpu_fault(5);
            return true;
        }
        case 0x68: push32(fetch32()); return true;
        case 0x6A: push32((uint32_t) (int32_t) (int8_t) fetch8()); return true;
        case 0x69: case 0x6B: {
            modregrm();
            const uint32_t value = readrm32(rm);
            const uint32_t factor = opcode == 0x69 ? fetch32() : (uint32_t) (int32_t) (int8_t) fetch8();
            dwordregs[reg] = imul32(value, factor);
            return true;
        }
        case 0x81: case 0x83: {
            modregrm();
            const uint32_t value = readrm32(rm);
            const uint32_t immediate = opcode == 0x81 ? fetch32() : (uint32_t) (int32_t) (int8_t) fetch8();
            const uint32_t result = alu32(reg, value, immediate);
            if (reg != 7) writerm32(rm, result);
            return true;
        }
        case 0x85: modregrm(); alu32(4, readrm32(rm), dwordregs[reg]); return true;
        case 0x87: {
            modregrm();
            const uint32_t value = readrm32(rm);
            writerm32(rm, dwordregs[reg]);
            dwordregs[reg] = value;
            return true;
        }
        case 0x89: modregrm(); writerm32(rm, dwordregs[reg]); return true;
        case 0x8B: modregrm(); dwordregs[reg] = readrm32(rm); return true;
        case 0x8C: // MOV r32/m16, Sreg
            modregrm();
            if (reg > reggs) {
                cpu_fault(6);
            } else if (mode == 3) {
                dwordregs[rm] = segregs[reg];
            } else {
                getea(rm);
                mem_write16(ea, segregs[reg]);
            }
            return true;
        case 0x8D: // LEA r32
            modregrm();
            if (mode == 3) {
                cpu_fault(6);
                return true;
            }
            getea(rm);
            dwordregs[reg] = ea - useseg;
            return true;
        case 0x8F: // POP r/m32
            modregrm();
            if (reg) {
                cpu_fault(6);
                return true;
            }
            writerm32(rm, pop32());
            return true;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
            const uint32_t value = dwordregs[regax];
            dwordregs[regax] = dwordregs[opcode & 7];
            dwordregs[opcode & 7] = value;
            return true;
        }
        case 0x98: // CWDE
            dwordregs[regax] = (uint32_t) (int32_t) (int16_t) CPU_AX;
            return true;
        case 0x99: // CDQ
            dwordregs[regdx] = (int32_t) dwordregs[regax] < 0 ? 0xFFFFFFFFu : 0;
            return true;
        case 0x9A: { // CALL ptr16:32
            const uint32_t offset = fetch32();
            far_call(fetch16(), offset, true);
            return true;
        }
        case 0x9C: check_v86_iopl(); push32(makeflagsword()); return true; // PUSHFD: VM and RF read as 0
        case 0x9D: check_v86_iopl(); set_flags(pop32(), 0x7FD5); return true; // POPFD: VM and RF stay
        case 0xA9: alu32(4, dwordregs[regax], fetch32()); return true;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            dwordregs[opcode & 7] = fetch32();
            return true;
        case 0xC1: modregrm(); { const uint32_t value = readrm32(rm); writerm32(rm, shift32(value, fetch8())); } return true;
        case 0xD1: modregrm(); writerm32(rm, shift32(readrm32(rm), 1)); return true;
        case 0xD3: modregrm(); writerm32(rm, shift32(readrm32(rm), CPU_CL)); return true;
        case 0xC2: { // RET imm16, 32-bit return address
            const uint16_t release = fetch16();
            ip = pop32();
            stack_add(release);
            return true;
        }
        case 0xC3: ip = pop32(); return true;
        case 0xC4: case 0xC5: // LES / LDS r32, m16:32
            modregrm();
            if (mode == 3) {
                cpu_fault(6);
                return true;
            }
            getea(rm);
            dwordregs[reg] = mem_read32(ea); // m16:32: offset, then selector
            load_segment(opcode == 0xC4 ? reges : regds, mem_read16(ea + 4));
            return true;
        case 0xC7: // MOV r/m32, imm32
            modregrm();
            if (reg) {
                cpu_fault(6);
                return true;
            }
            if (mode < 3) getea(rm); // the displacement comes before the immediate
            {
                const uint32_t immediate = fetch32();
                if (mode < 3) mem_write32(ea, immediate);
                else dwordregs[rm] = immediate;
            }
            return true;
        case 0xC8: { // ENTER imm16, imm8
            const uint16_t frame_size = fetch16();
            const uint8_t level = fetch8() & 31;
            push32(dwordregs[regbp]);
            const uint32_t frame = dwordregs[regsp]; // all of ESP, also with a 16-bit stack
            for (uint8_t i = 1; i < level; i++) {
                if (stack32) dwordregs[regbp] -= 4;
                else CPU_BP -= 4;
                push32(mem_read32(SS_BASE + (stack32 ? dwordregs[regbp] : CPU_BP)));
            }
            if (level) push32(frame);
            dwordregs[regbp] = frame;
            stack_add(-(int32_t) frame_size);
            return true;
        }
        case 0xC9: // LEAVE
            stack_to_frame();
            dwordregs[regbp] = pop32();
            return true;
        case 0xCA: case 0xCB: // RETF, 32-bit slots
            far_return(true, opcode == 0xCA ? fetch16() : 0);
            return true;
        case 0xCF: // IRETD
            interrupt_return(true);
            return true;
        case 0xE5: dwordregs[regax] = port_read_sized(fetch8(), 4); return true;
        case 0xE7: port_write_sized(fetch8(), dwordregs[regax], 4); return true;
        case 0xED: dwordregs[regax] = port_read_sized(CPU_DX, 4); return true;
        case 0xEF: port_write_sized(CPU_DX, dwordregs[regax], 4); return true;
        case 0xE8: { // CALL rel32
            const uint32_t displacement = fetch32();
            push32(ip);
            ip += displacement;
            return true;
        }
        case 0xE9: { // JMP rel32
            const uint32_t displacement = fetch32();
            ip += displacement;
            return true;
        }
        case 0xEA: { // JMP ptr16:32
            const uint32_t offset = fetch32();
            far_jump(fetch16(), offset);
            return true;
        }
        case 0xF7: modregrm(); group3_32(); return true;
        case 0xFF: {
            modregrm();
            switch (reg) {
                case 0: writerm32(rm, inc_dec(readrm32(rm), false, 32)); return true;
                case 1: writerm32(rm, inc_dec(readrm32(rm), true, 32)); return true;
                case 2: { // CALL near r/m32
                    const uint32_t target = readrm32(rm);
                    push32(ip);
                    ip = target;
                    return true;
                }
                case 3: // CALL far m16:32
                    getea(rm);
                    far_call(mem_read16(ea + 4), mem_read32(ea), true);
                    return true;
                case 4: ip = readrm32(rm); return true;
                case 5: // JMP far m16:32
                    getea(rm);
                    far_jump(mem_read16(ea + 4), mem_read32(ea));
                    return true;
                case 6: push32(readrm32(rm)); return true;
                default: cpu_fault(6); return true;
            }
        }
    }
    return false;
}
