// Protected mode and paging of the 386, from the Intel 80386 Programmer's Reference Manual
// (docs/intel/i386-prm-1986.txt): chapters 5 (segments, paging), 6 (protection), 9 (interrupts)
// 15 (V86 mode) and 17 (instructions). Included by cpu.c after cpu386.c.inl.
// Fast, not exact: no segment limit or write-protect checks on memory access, task switches
// only with a 386 TSS, no debug traps.
#pragma once

static uint32_t cr2, cr3;
static uint32_t debug_registers[8]; // only stored

typedef struct {
    uint32_t base, limit;
} table_register_t;

static table_register_t gdtr, idtr;
static uint16_t ldtr_selector, tr_selector;
static table_register_t ldtr, tr;
static bool tss32; // TR holds a 386 TSS (else a 286 TSS)

// Descriptor tables and the TSS are read at privilege 0, also from CPL 3 (for the paging checks)
static uint8_t supervisor_access;

// ---- Paging (chapter 5.2): page walk with a small direct-mapped TLB -------------------------
// While CR0.PG is set, read86..writedw86 point to the paged_ functions below; they translate the
// linear address and call the physical functions that were there before.

#define TLB_SIZE 64

typedef struct {
    uint32_t page; // linear address >> 12; ~0 = empty
    uint32_t frame; // physical address of the page
    bool user, writable, dirty; // U/S and R/W of directory and table together, D set
} tlb_entry_t;

static tlb_entry_t tlb[TLB_SIZE];
static read86_t physical_read8;
static read86w_t physical_read16;
static read86dw_t physical_read32;
static write86_t physical_write8;
static write86w_t physical_write16;
static write86dw_t physical_write32;

static void flush_tlb(void) {
    for (int i = 0; i < TLB_SIZE; i++) tlb[i].page = ~0u;
}

static __attribute__((noreturn)) void page_fault(uint32_t linear, uint8_t error_code) {
    cr2 = linear;
    raise_fault(14, error_code);
}

static __attribute__((noinline)) uint32_t page_walk(uint32_t linear, bool write) {
    const bool user = cpl == 3 && !supervisor_access;
    const uint8_t error_code = (write ? 2 : 0) | (user ? 4 : 0);
    const uint32_t directory_address = (cr3 & 0xFFFFF000u) + (linear >> 22) * 4;
    const uint32_t directory = physical_read32(directory_address);
    if (!(directory & 1)) page_fault(linear, error_code);
    const uint32_t table_address = (directory & 0xFFFFF000u) + ((linear >> 12) & 0x3FF) * 4;
    const uint32_t table = physical_read32(table_address);
    if (!(table & 1)) page_fault(linear, error_code);
    const bool user_page = directory & table & 4, writable = directory & table & 2;
    if (user && (!user_page || (write && !writable))) page_fault(linear, error_code | 1);
    // the 386 sets the accessed bits, and the dirty bit on a write
    if (!(directory & 0x20)) physical_write32(directory_address, directory | 0x20);
    const uint32_t new_table = table | 0x20 | (write ? 0x40 : 0);
    if (new_table != table) physical_write32(table_address, new_table);
    tlb_entry_t *entry = &tlb[(linear >> 12) & (TLB_SIZE - 1)];
    *entry = (tlb_entry_t) {linear >> 12, new_table & 0xFFFFF000u, user_page, writable, new_table & 0x40};
    return entry->frame | (linear & 0xFFF);
}

static INLINE uint32_t translate(uint32_t linear, bool write) {
    const tlb_entry_t *entry = &tlb[(linear >> 12) & (TLB_SIZE - 1)];
    const bool user = cpl == 3 && !supervisor_access;
    if (entry->page == linear >> 12 && (!write || entry->dirty) &&
        (!user || (entry->user && (!write || entry->writable)))) {
        return entry->frame | (linear & 0xFFF);
    }
    return page_walk(linear, write);
}

// A word or dword that crosses a page goes in parts
static uint8_t paged_read8(uint32_t a) { return physical_read8(translate(a, false)); }
static uint16_t paged_read16(uint32_t a) {
    if ((a & 0xFFF) == 0xFFF) return paged_read8(a) | paged_read8(a + 1) << 8;
    return physical_read16(translate(a, false));
}
static uint32_t paged_read32(uint32_t a) {
    if ((a & 0xFFF) > 0xFFC) return paged_read16(a) | (uint32_t) paged_read16(a + 2) << 16;
    return physical_read32(translate(a, false));
}
static void paged_write8(uint32_t a, uint8_t v) { physical_write8(translate(a, true), v); }
static void paged_write16(uint32_t a, uint16_t v) {
    if ((a & 0xFFF) == 0xFFF) {
        paged_write8(a, (uint8_t) v);
        paged_write8(a + 1, v >> 8);
    } else {
        physical_write16(translate(a, true), v);
    }
}
static void paged_write32(uint32_t a, uint32_t v) {
    if ((a & 0xFFF) > 0xFFC) {
        paged_write16(a, (uint16_t) v);
        paged_write16(a + 2, v >> 16);
    } else {
        physical_write32(translate(a, true), v);
    }
}

static void set_paging(bool on) {
    static uint32_t fast_size; // the direct RAM path is only right without paging
    if (on == (read86 == paged_read8)) return;
    if (on) {
        fast_size = fast_memory_size;
        fast_memory_size = 0;
        physical_read8 = read86, physical_read16 = readw86, physical_read32 = readdw86;
        physical_write8 = write86, physical_write16 = writew86, physical_write32 = writedw86;
        read86 = paged_read8, readw86 = paged_read16, readdw86 = paged_read32;
        write86 = paged_write8, writew86 = paged_write16, writedw86 = paged_write32;
    } else {
        fast_memory_size = fast_size;
        read86 = physical_read8, readw86 = physical_read16, readdw86 = physical_read32;
        write86 = physical_write8, writew86 = physical_write16, writedw86 = physical_write32;
    }
    flush_tlb();
}

static void set_cr0(uint32_t value) {
    value |= CR0_ET;
    const uint32_t changed = cr0 ^ value;
    cr0 = value;
    protected_mode = (cr0 & CR0_PE) && !v86;
    if (!(cr0 & CR0_PE)) cpl = 0;
    if (changed & CR0_PG) set_paging(cr0 & CR0_PG);
}

// ---- Descriptors (chapter 5.1) ----------------------------------------------------------------

typedef struct {
    uint32_t low, high;
} descriptor_t;

static INLINE uint32_t descriptor_base(descriptor_t d) {
    return d.low >> 16 | (d.high & 0xFF) << 16 | (d.high & 0xFF000000u);
}

static INLINE uint32_t descriptor_limit(descriptor_t d) {
    const uint32_t limit = (d.low & 0xFFFF) | (d.high & 0xF0000);
    return d.high & (1u << 23) ? limit << 12 | 0xFFF : limit; // G: 4K units
}

static INLINE bool is_present(descriptor_t d) { return d.high >> 15 & 1; }
static INLINE uint8_t descriptor_dpl(descriptor_t d) { return d.high >> 13 & 3; }
static INLINE bool is_segment(descriptor_t d) { return d.high >> 12 & 1; } // code or data, not system
static INLINE bool is_code(descriptor_t d) { return is_segment(d) && (d.high >> 11 & 1); }
static INLINE bool is_conforming(descriptor_t d) { return is_code(d) && (d.high >> 10 & 1); }
static INLINE uint8_t system_type(descriptor_t d) { return d.high >> 8 & 0xF; }
static INLINE bool is_big(descriptor_t d) { return d.high >> 22 & 1; } // D/B: 32-bit
static INLINE uint32_t gate_offset(descriptor_t d) { // 286 gates (type < 8) have a 16-bit offset
    return (d.low & 0xFFFF) | (system_type(d) & 8 ? d.high & 0xFFFF0000u : 0);
}

static uint32_t system_read32(uint32_t linear) {
    supervisor_access = 1;
    const uint32_t value = readdw86(linear);
    supervisor_access = 0;
    return value;
}

static uint16_t system_read16(uint32_t linear) {
    supervisor_access = 1;
    const uint16_t value = readw86(linear);
    supervisor_access = 0;
    return value;
}

static void system_write16(uint32_t linear, uint16_t value) {
    supervisor_access = 1;
    writew86(linear, value);
    supervisor_access = 0;
}

static void system_write32(uint32_t linear, uint32_t value) {
    supervisor_access = 1;
    writedw86(linear, value);
    supervisor_access = 0;
}

// Linear address of a selector's descriptor in the GDT or LDT; 0 if it is outside the table.
static uint32_t descriptor_address(uint16_t selector) {
    const table_register_t *table = selector & 4 ? &ldtr : &gdtr;
    if ((selector | 7u) > table->limit) return 0;
    return table->base + (selector & ~7u);
}

// Reads a descriptor; false for a null selector or one outside its table.
static bool read_descriptor(uint16_t selector, descriptor_t *d) {
    const uint32_t address = descriptor_address(selector);
    if (!(selector & ~3u) || !address) return false;
    d->low = system_read32(address);
    d->high = system_read32(address + 4);
    return true;
}

static void mark_accessed(uint16_t selector, descriptor_t *d) {
    if (!(d->high & 0x100)) {
        d->high |= 0x100;
        system_write32(descriptor_address(selector) + 4, d->high);
    }
}

// SS needs a writable data segment at the CPL; the others a data or readable code segment that the
// CPL and RPL may use (DPL >= both, or conforming code).
void load_segment_protected(uint8_t segment, uint16_t selector) {
    if (!(selector & ~3u)) { // a null selector: allowed, but not for SS
        if (segment == regss) raise_fault(13, 0);
        segregs[segment] = selector;
        segment_base[segment] = 0;
        return;
    }
    descriptor_t d;
    if (!read_descriptor(selector, &d) || !is_segment(d)) raise_fault(13, selector & ~3u);
    const bool readable_or_writable = d.high >> 9 & 1; // data: writable; code: readable
    const bool allowed = segment == regss
                             ? !is_code(d) && readable_or_writable && (selector & 3) == cpl && descriptor_dpl(d) == cpl
                             : (!is_code(d) || readable_or_writable) &&
                               (is_conforming(d) || (descriptor_dpl(d) >= cpl && descriptor_dpl(d) >= (selector & 3)));
    if (!allowed) raise_fault(13, selector & ~3u);
    if (!is_present(d)) raise_fault(segment == regss ? 12 : 11, selector & ~3u);
    mark_accessed(selector, &d);
    segregs[segment] = selector;
    segment_base[segment] = descriptor_base(d);
    if (segment == regss) stack32 = is_big(d);
    if (segment == regcs) code32 = is_big(d);
}

// ---- Control transfers (chapter 6.3) ----------------------------------------------------------

// The descriptor of a far transfer target: #GP if it is not code, #NP if not present.
static descriptor_t code_descriptor(uint16_t selector) {
    descriptor_t d;
    if (!(selector & ~3u)) raise_fault(13, 0);
    if (!read_descriptor(selector, &d) || !is_code(d)) raise_fault(13, selector & ~3u);
    if (!is_present(d)) raise_fault(11, selector & ~3u);
    return d;
}

static void set_code_segment(uint16_t selector, descriptor_t d, uint8_t privilege, uint32_t offset) {
    mark_accessed(selector, &d);
    segregs[regcs] = (selector & ~3u) | privilege;
    segment_base[regcs] = descriptor_base(d);
    code32 = is_big(d);
    cpl = privilege;
    ip = offset;
}

static void load_stack(uint16_t selector, uint32_t esp) {
    descriptor_t d;
    if (!read_descriptor(selector, &d) || !is_segment(d)) raise_fault(10, selector & ~3u); // #TS
    if (!is_present(d)) raise_fault(12, selector & ~3u);
    segregs[regss] = selector;
    segment_base[regss] = descriptor_base(d);
    stack32 = is_big(d);
    if (stack32) dwordregs[regsp] = esp;
    else CPU_SP = esp;
}

// The stack for a more privileged level comes from the TSS: SS0:ESP0 and so on.
static void load_inner_stack(uint8_t level) {
    if (tss32) {
        const uint32_t esp = system_read32(tr.base + 4 + level * 8);
        load_stack(system_read16(tr.base + 8 + level * 8), esp);
    } else {
        const uint16_t sp = system_read16(tr.base + 2 + level * 4);
        load_stack(system_read16(tr.base + 4 + level * 4), sp);
    }
}

static void push_sized(uint32_t value, bool size32) {
    if (size32) push32(value);
    else push((uint16_t) value);
}

static uint32_t pop_sized(bool size32) {
    return size32 ? pop32() : pop();
}

// A far JMP or CALL straight to a code segment keeps the CPL: the segment must be conforming with
// DPL <= CPL, or non-conforming with DPL = CPL and RPL <= CPL.
static void check_direct_code(uint16_t selector, descriptor_t code) {
    const bool allowed = is_conforming(code) ? descriptor_dpl(code) <= cpl
                                             : descriptor_dpl(code) == cpl && (selector & 3) <= cpl;
    if (!allowed) raise_fault(13, selector & ~3u);
}

// A call gate must be visible from the CPL and the RPL of its selector.
static void check_gate(uint16_t selector, descriptor_t gate) {
    if (descriptor_dpl(gate) < cpl || descriptor_dpl(gate) < (selector & 3)) raise_fault(13, selector & ~3u);
    if (!is_present(gate)) raise_fault(11, selector & ~3u);
}

// LLDT and task switches: a null selector leaves no LDT.
static void load_ldtr(uint16_t selector) {
    descriptor_t d;
    ldtr_selector = selector;
    ldtr = (table_register_t) {0, 0};
    if (!(selector & ~3u)) return;
    if (!read_descriptor(selector, &d) || is_segment(d) || system_type(d) != 2) raise_fault(13, selector & ~3u);
    if (!is_present(d)) raise_fault(11, selector & ~3u);
    ldtr = (table_register_t) {descriptor_base(d), descriptor_limit(d)};
}

static void set_tss_busy(uint16_t selector, bool busy) {
    const uint32_t address = descriptor_address(selector) + 4;
    const uint32_t high = system_read32(address);
    system_write32(address, busy ? high | 0x200 : high & ~0x200u);
}

// ---- Task switch (chapter 7), 386 TSS only ----------------------------------------------------
// The current state goes into the current TSS, the new one comes from the TSS (or task gate) at
// selector. EIP must already be the return address. CALL (and INT through a task gate) links the
// new task back to the old one and sets NT; IRET with NT goes back along that link.

enum { TASK_JMP, TASK_CALL, TASK_IRET };

// The 386 TSS: EIP at 20h, EFLAGS 24h, EAX..EDI 28h-44h, ES CS SS DS FS GS 48h-5Ch, LDT 60h, CR3 1Ch
static void task_switch(uint16_t selector, uint8_t reason, int32_t error_code) {
    descriptor_t d;
    if (!read_descriptor(selector, &d) || is_segment(d)) raise_fault(13, selector & ~3u);
    if (system_type(d) == 5) { // a task gate in the GDT or LDT
        if (!is_present(d)) raise_fault(11, selector & ~3u);
        selector = d.low >> 16;
        if ((selector & 4) || !read_descriptor(selector, &d) || is_segment(d)) raise_fault(13, selector & ~3u);
    }
    if (system_type(d) != (reason == TASK_IRET ? 0xB : 9)) { // a busy, or an available, 386 TSS
        if ((system_type(d) & 7) == 1 || (system_type(d) & 7) == 3) printf("[CPU] 286 TSS is not emulated\r\n");
        raise_fault(13, selector & ~3u);
    }
    if (!is_present(d)) raise_fault(11, selector & ~3u);
    if (descriptor_limit(d) < 0x67 || !tss32) raise_fault(10, selector & ~3u);

    uint32_t flags = makeflagsword() | (x86_flags.value & 0x30000);
    if (reason == TASK_IRET) flags &= ~0x4000u; // the old task leaves its nested state
    system_write32(tr.base + 0x20, ip);
    system_write32(tr.base + 0x24, flags);
    for (int r = 0; r < 8; r++) system_write32(tr.base + 0x28 + r * 4, dwordregs[r]);
    static const uint8_t order[6] = {reges, regcs, regss, regds, regfs, reggs};
    for (int i = 0; i < 6; i++) system_write16(tr.base + 0x48 + i * 4, segregs[order[i]]);
    if (reason != TASK_CALL) set_tss_busy(tr_selector, false);
    if (reason != TASK_IRET) set_tss_busy(selector, true);

    const uint32_t base = descriptor_base(d);
    if (reason == TASK_CALL) system_write16(base, tr_selector); // back link
    // read the whole new state before CR3 changes the address space
    const uint32_t new_cr3 = system_read32(base + 0x1C), new_eip = system_read32(base + 0x20);
    uint32_t new_flags = system_read32(base + 0x24), registers[8];
    uint16_t segments[6];
    for (int r = 0; r < 8; r++) registers[r] = system_read32(base + 0x28 + r * 4);
    for (int i = 0; i < 6; i++) segments[i] = system_read16(base + 0x48 + i * 4);
    const uint16_t ldt = system_read16(base + 0x60);

    tr_selector = selector;
    tr = (table_register_t) {base, descriptor_limit(d)};
    cr0 |= 8; // TS
    if (cr0 & CR0_PG) {
        cr3 = new_cr3;
        flush_tlb();
    }
    if (reason == TASK_CALL) new_flags |= 0x4000; // NT
    flags_now()->value = (new_flags & 0x37FD5) | 2;
    memcpy(dwordregs, registers, sizeof registers);
    load_ldtr(ldt);
    protected_mode = !v86;
    if (v86) { // a V86 task: segments as in real mode
        cpl = 3;
        code32 = stack32 = 0;
        for (int i = 0; i < 6; i++) load_segment(order[i], segments[i]);
        ip = new_eip & 0xFFFF;
    } else {
        const uint16_t cs = segments[1];
        set_code_segment(cs, code_descriptor(cs), cs & 3, new_eip);
        for (int i = 0; i < 6; i++) {
            if (order[i] != regcs) load_segment_protected(order[i], segments[i]);
        }
    }
    if (error_code != NO_ERROR_CODE) push32(error_code);
}

static void far_jump(uint16_t selector, uint32_t offset) {
    if (!protected_mode) {
        ip = offset;
        load_segment(regcs, selector);
        return;
    }
    descriptor_t d;
    if (!read_descriptor(selector, &d)) raise_fault(13, selector & ~3u);
    if (!is_segment(d)) { // a call gate, to the same privilege level
        const uint8_t type = system_type(d);
        if (type != 4 && type != 0xC) {
            task_switch(selector, TASK_JMP, NO_ERROR_CODE);
            return;
        }
        check_gate(selector, d);
        selector = d.low >> 16;
        offset = gate_offset(d);
        const descriptor_t code = code_descriptor(selector);
        if (is_conforming(code) ? descriptor_dpl(code) > cpl : descriptor_dpl(code) != cpl) raise_fault(13, selector & ~3u);
        set_code_segment(selector, code, cpl, offset);
        return;
    }
    const descriptor_t code = code_descriptor(selector);
    check_direct_code(selector, code);
    set_code_segment(selector, code, cpl, offset);
}

static void far_call(uint16_t selector, uint32_t offset, bool size32) {
    if (!protected_mode) {
        push_sized(CPU_CS, size32);
        push_sized(ip, size32);
        ip = offset;
        load_segment(regcs, selector);
        return;
    }
    descriptor_t d;
    if (!read_descriptor(selector, &d)) raise_fault(13, selector & ~3u);
    if (is_segment(d)) {
        const descriptor_t code = code_descriptor(selector);
        check_direct_code(selector, code);
        push_sized(CPU_CS, size32);
        push_sized(ip, size32);
        set_code_segment(selector, code, cpl, offset);
        return;
    }
    const uint8_t type = system_type(d);
    if (type != 4 && type != 0xC) {
        task_switch(selector, TASK_CALL, NO_ERROR_CODE);
        return;
    }
    check_gate(selector, d);
    const bool gate32 = type == 0xC;
    const uint16_t target = d.low >> 16;
    const descriptor_t code = code_descriptor(target);
    const uint8_t privilege = is_conforming(code) ? cpl : descriptor_dpl(code);
    if (privilege > cpl) raise_fault(13, target & ~3u);
    if (privilege < cpl) { // to an inner level: new stack, copy the parameters
        const uint8_t count = d.high & 0x1F;
        const uint16_t old_ss = CPU_SS;
        const uint32_t old_esp = stack_top(), old_base = SS_BASE;
        uint32_t parameters[32];
        for (uint8_t i = 0; i < count; i++) {
            const uint32_t address = old_base + old_esp + i * (gate32 ? 4 : 2);
            parameters[i] = gate32 ? readdw86(address) : readw86(address);
        }
        cpl = privilege;
        load_inner_stack(privilege);
        push_sized(old_ss, gate32);
        push_sized(old_esp, gate32);
        for (int i = count - 1; i >= 0; i--) push_sized(parameters[i], gate32);
    }
    push_sized(CPU_CS, gate32);
    push_sized(ip, gate32);
    set_code_segment(target, code, privilege, gate_offset(d));
}

// After a return to an outer level, ES, DS, FS and GS that the new level may not use become null.
static void null_inner_segments(void) {
    static const uint8_t data_registers[4] = {reges, regds, regfs, reggs};
    for (int i = 0; i < 4; i++) {
        const uint8_t segment = data_registers[i];
        descriptor_t d;
        if (read_descriptor(segregs[segment], &d) && !is_conforming(d) && descriptor_dpl(d) < cpl) {
            segregs[segment] = 0;
            segment_base[segment] = 0;
        }
    }
}

static void far_return(bool size32, uint16_t release) {
    const uint32_t offset = pop_sized(size32);
    const uint16_t selector = pop_sized(size32);
    stack_add(release);
    if (!protected_mode) {
        ip = offset;
        load_segment(regcs, selector);
        return;
    }
    const uint8_t privilege = selector & 3;
    if (privilege < cpl) raise_fault(13, selector & ~3u);
    const descriptor_t code = code_descriptor(selector);
    if (privilege > cpl) { // to an outer level: its stack was saved after the return address
        const uint32_t esp = pop_sized(size32);
        const uint16_t ss = pop_sized(size32);
        set_code_segment(selector, code, privilege, offset);
        load_stack(ss, esp);
        stack_add(release);
        null_inner_segments();
        return;
    }
    set_code_segment(selector, code, privilege, offset);
}

// ---- Interrupts and exceptions (chapter 9) ------------------------------------------------------

static void protected_interrupt(uint8_t vector, int32_t error_code, bool software) {
    const bool from_v86 = v86;
    if (from_v86 && software && iopl < 3) raise_fault(13, 0);
    const uint16_t idt_error = vector * 8 + 2 + !software; // bit 0 (EXT): not caused by INT n
    if (vector * 8u + 7 > idtr.limit) raise_fault(13, idt_error);
    const descriptor_t gate = {system_read32(idtr.base + vector * 8), system_read32(idtr.base + vector * 8 + 4)};
    const uint8_t type = system_type(gate);
    if (type == 5) { // a task gate: the TSS selector is in it
        if (software && descriptor_dpl(gate) < cpl) raise_fault(13, idt_error);
        if (!is_present(gate)) raise_fault(11, idt_error);
        task_switch(gate.low >> 16, TASK_CALL, error_code);
        return;
    }
    if (is_segment(gate) || (type & 7) < 6) raise_fault(13, idt_error); // 286/386 interrupt and trap gates
    if (software && descriptor_dpl(gate) < cpl) raise_fault(13, idt_error);
    if (!is_present(gate)) raise_fault(11, idt_error);
    const bool gate32 = type & 8;
    const uint16_t selector = gate.low >> 16;
    const descriptor_t code = code_descriptor(selector);
    const uint8_t privilege = is_conforming(code) ? cpl : descriptor_dpl(code);
    if (privilege > cpl || (from_v86 && privilege)) raise_fault(13, selector & ~3u);
    const uint32_t flags = makeflagsword() | (x86_flags.value & 0x30000); // with RF and VM
    if (privilege < cpl) { // from V86 mode too: CPL 3 to 0
        const uint16_t old_ss = CPU_SS;
        const uint32_t old_esp = dwordregs[regsp];
        cpl = privilege;
        load_inner_stack(privilege);
        if (from_v86) { // the V86 segment registers go on the stack and become null
            static const uint8_t saved[4] = {reggs, regfs, regds, reges};
            for (int i = 0; i < 4; i++) {
                push_sized(segregs[saved[i]], gate32);
                segregs[saved[i]] = 0;
                segment_base[saved[i]] = 0;
            }
        }
        push_sized(old_ss, gate32);
        push_sized(old_esp, gate32);
    }
    push_sized(flags, gate32);
    push_sized(CPU_CS, gate32);
    push_sized(ip, gate32);
    if (error_code != NO_ERROR_CODE) push_sized(error_code, gate32);
    set_code_segment(selector, code, privilege, gate_offset(gate));
    x86_flags.value &= ~0x34100u; // TF, NT, RF, VM
    protected_mode = 1;
    if (!(type & 1)) ifl = 0; // an interrupt gate, not a trap gate
}

// IRETD at CPL 0 with VM set in the popped EFLAGS: the V86 task's ESP, SS, ES, DS, FS, GS follow.
static void enter_v86(uint32_t offset, uint16_t selector, uint32_t flags) {
    const uint32_t esp = pop32();
    const uint16_t ss = pop32(), es = pop32(), ds = pop32(), fs = pop32(), gs = pop32();
    flags_now()->value = flags & 0x37FD5;
    protected_mode = 0; // segment loads work as in real mode now
    cpl = 3;
    code32 = stack32 = 0;
    load_segment(regcs, selector);
    load_segment(regss, ss);
    load_segment(reges, es);
    load_segment(regds, ds);
    load_segment(regfs, fs);
    load_segment(reggs, gs);
    dwordregs[regsp] = esp;
    ip = offset & 0xFFFF;
}

static void interrupt_return(bool size32) {
    if (v86) check_v86_iopl(); // with IOPL 3 it works as in real mode
    if (!protected_mode) {
        ip = pop_sized(size32);
        load_segment(regcs, pop_sized(size32));
        set_flags(pop_sized(size32), size32 ? 0x17FD5 : 0x7FD5);
        return;
    }
    if (nt) { // return to the task in the back link
        task_switch(system_read16(tr.base), TASK_IRET, NO_ERROR_CODE);
        return;
    }
    const uint32_t offset = pop_sized(size32);
    const uint16_t selector = pop_sized(size32);
    const uint32_t flags = pop_sized(size32);
    if (size32 && (flags & 0x20000) && cpl == 0) {
        enter_v86(offset, selector, flags);
        return;
    }
    const uint8_t privilege = selector & 3;
    if (privilege < cpl) raise_fault(13, selector & ~3u);
    const descriptor_t code = code_descriptor(selector);
    set_flags(flags, size32 ? 0x17FD5 : 0x7FD5); // with the old CPL
    if (privilege > cpl) {
        const uint32_t esp = pop_sized(size32);
        const uint16_t ss = pop_sized(size32);
        set_code_segment(selector, code, privilege, offset);
        load_stack(ss, esp);
        null_inner_segments();
        return;
    }
    set_code_segment(selector, code, privilege, offset);
}

// An interrupt that does not come from INT n: an IRQ or the single-step trap.
static void external_interrupt(uint8_t vector) {
    if (cr0 & CR0_PE) protected_interrupt(vector, NO_ERROR_CODE, false);
    else intcall86(vector);
}

// exec86() comes here after raise_fault(): the instruction is undone (EIP, ESP) and the exception
// goes through the IDT. A fault while delivering one is a double fault; one more resets the CPU.
static void deliver_fault(void) {
    static uint8_t depth;
    supervisor_access = 0;
    ip = firstip;
    dwordregs[regsp] = first_esp;
    if (depth == 2) {
        printf("[CPU] triple fault: reset\r\n");
        depth = 0;
        reset86();
        return;
    }
    if (depth++ == 1) {
        fault_vector = 8;
        fault_error = 0;
    }
    protected_interrupt(fault_vector, fault_error, false);
    depth = 0;
}

// IN, OUT, INS, OUTS: every port byte needs a 0 bit in the I/O permission bitmap of the 386 TSS
// (its offset is at 66h); otherwise #GP(0).
static void check_io_permission(uint16_t port, uint8_t size) {
    if (!tss32) raise_fault(13, 0);
    const uint32_t byte = system_read16(tr.base + 0x66) + port / 8u;
    if (byte + 1 > tr.limit) raise_fault(13, 0);
    if (system_read16(tr.base + byte) >> (port & 7) & ((1u << size) - 1)) raise_fault(13, 0);
}

// ---- System instructions ----------------------------------------------------------------------

static void privileged(void) {
    if (cpl) raise_fault(13, 0);
}

// VERR and VERW: can this selector be read (or written) at the current privilege?
static bool verify_segment(uint16_t selector, bool write) {
    descriptor_t d;
    if (!read_descriptor(selector, &d) || !is_segment(d)) return false;
    if (!is_conforming(d) && (descriptor_dpl(d) < cpl || descriptor_dpl(d) < (selector & 3))) return false;
    const bool code = is_code(d), readable_or_writable = d.high >> 9 & 1;
    return write ? !code && readable_or_writable : !code || readable_or_writable;
}

// LAR and LSL: ZF = 1 and the access rights or the limit, if the descriptor is visible.
static void load_access_or_limit(bool limit) {
    modregrm();
    const uint16_t selector = readrm16(rm);
    descriptor_t d;
    zf = 0;
    if (!read_descriptor(selector, &d)) return;
    // system types LAR accepts: TSS, LDT, gates; LSL: TSS and LDT
    const uint16_t valid_types = limit ? 0x0A0E : 0x1A3E;
    if (!is_segment(d) && !(valid_types >> system_type(d) & 1)) return;
    if (!is_conforming(d) && (descriptor_dpl(d) < cpl || descriptor_dpl(d) < (selector & 3))) return;
    zf = 1;
    putreg_sized(reg, limit ? descriptor_limit(d) : d.high & (opsize32 ? 0x00FFFF00u : 0xFF00));
}

// 0F 00 group: SLDT, STR, LLDT, LTR, VERR, VERW. Only in protected mode.
static bool group6(void) {
    modregrm();
    switch (reg) {
        case 0: writerm16(rm, ldtr_selector); return true;
        case 1: writerm16(rm, tr_selector); return true;
        case 2: // LLDT
            privileged();
            load_ldtr(readrm16(rm));
            return true;
        case 3: { // LTR: an available TSS, which becomes busy
            privileged();
            const uint16_t selector = readrm16(rm);
            descriptor_t d;
            if (!read_descriptor(selector, &d) || is_segment(d) || (system_type(d) & 7) != 1) raise_fault(13, selector & ~3u);
            if (!is_present(d)) raise_fault(11, selector & ~3u);
            d.high |= 0x200;
            system_write32(descriptor_address(selector) + 4, d.high);
            tr_selector = selector;
            tr = (table_register_t) {descriptor_base(d), descriptor_limit(d)};
            tss32 = system_type(d) & 8;
            return true;
        }
        case 4:
        case 5:
            zf = verify_segment(readrm16(rm), reg == 5);
            return true;
    }
    return false;
}

// 0F 01 group: SGDT, SIDT, LGDT, LIDT, SMSW, LMSW
static bool group7(void) {
    modregrm();
    switch (reg) {
        case 0:
        case 1: { // SGDT, SIDT: 16-bit limit, 32-bit base
            if (mode == 3) return false;
            getea(rm);
            const table_register_t *table = reg ? &idtr : &gdtr;
            writew86(ea, (uint16_t) table->limit);
            writedw86(ea + 2, table->base);
            return true;
        }
        case 2:
        case 3: { // LGDT, LIDT: a 16-bit operand size loads a 24-bit base
            if (mode == 3) return false;
            privileged();
            getea(rm);
            table_register_t *table = reg == 3 ? &idtr : &gdtr;
            table->limit = readw86(ea);
            table->base = readdw86(ea + 2) & (opsize32 ? 0xFFFFFFFFu : 0x00FFFFFFu);
            return true;
        }
        case 4: // SMSW: a 32-bit register gets all of CR0
            if (mode == 3 && opsize32) dwordregs[rm] = cr0;
            else writerm16(rm, (uint16_t) cr0);
            return true;
        case 6: { // LMSW: PE, MP, EM, TS; PE cannot be cleared this way
            privileged();
            const uint16_t value = readrm16(rm);
            set_cr0((cr0 & ~0xEu) | (value & 0xF) | (cr0 & CR0_PE));
            return true;
        }
    }
    return false;
}

// MOV to and from CR0, CR2, CR3 and DR0-DR7. The r/m byte always names a register.
static bool move_special(uint8_t opcode) {
    modregrm();
    privileged();
    const bool to_register = !(opcode & 2), debug = opcode & 1;
    if (debug) {
        if (to_register) dwordregs[rm] = debug_registers[reg];
        else debug_registers[reg] = dwordregs[rm];
        return true;
    }
    uint32_t *control = reg == 0 ? &cr0 : reg == 2 ? &cr2 : reg == 3 ? &cr3 : NULL;
    if (!control) return false;
    if (to_register) {
        dwordregs[rm] = *control;
    } else if (reg == 0) {
        set_cr0(dwordregs[rm]);
    } else {
        *control = dwordregs[rm];
        if (reg == 3) flush_tlb();
    }
    return true;
}

// 0F system instructions; false if the opcode is not one of them (or not valid here).
static __attribute__((noinline)) bool op_0f_system(uint8_t opcode) { // cold: stays in flash
    switch (opcode) {
        case 0x00: return protected_mode && group6();
        case 0x01: return group7();
        case 0x02:
        case 0x03:
            if (!protected_mode) return false;
            load_access_or_limit(opcode == 0x03);
            return true;
        case 0x06: // CLTS
            privileged();
            cr0 &= ~8u;
            return true;
        case 0x20: case 0x21: case 0x22: case 0x23:
            return move_special(opcode);
        case 0xFF: // our BIOS escape (cpu.c)
            return bios_escape(fetch8());
    }
    return false;
}

// 63 ARPL: raise the RPL of the r/m selector to the RPL of the register. Only in protected mode.
static void adjust_rpl(void) {
    if (!protected_mode) {
        cpu_fault(6);
        return;
    }
    modregrm();
    const uint16_t selector = readrm16(rm), source = getreg16(reg);
    zf = (selector & 3) < (source & 3);
    if (zf) writerm16(rm, (selector & ~3u) | (source & 3));
}

static void reset_protected_mode(void) {
    set_cr0(CR0_ET);
    code32 = stack32 = 0;
    gdtr = (table_register_t) {0, 0xFFFF};
    idtr = (table_register_t) {0, 0x3FF};
    ldtr = tr = (table_register_t) {0, 0};
    ldtr_selector = tr_selector = 0;
    tss32 = false;
}
