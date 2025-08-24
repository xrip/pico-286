/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│ vi: set et ft=c ts=2 sts=2 sw=2 fenc=utf-8                               :vi │
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2022 Justine Alexandra Roberts Tunney                              │
│                                                                              │
│ Permission to use, copy, modify, and/or distribute this software for         │
│ any purpose with or without fee is hereby granted, provided that the         │
│ above copyright notice and this permission notice appear in all copies.      │
│                                                                              │
│ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL                │
│ WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                │
│ WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE             │
│ AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL         │
│ DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR        │
│ PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER               │
│ TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR             │
│ PERFORMANCE OF THIS SOFTWARE.                                                │
╚─────────────────────────────────────────────────────────────────────────────*/

// This FPU code was adapted for PCulator from the Blink emulator by Justine Tunney (aka jart)
// I want to write my own from scratch at some point, but this works well for now.
//
// Blink source code: https://github.com/jart/blink/

#include "fpu.h"
#include "cpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "emulator.h"

#define __builtin_unreachable() {}

#define FPUREG 0
#define MEMORY 1

#define DISP(x, y, z) ((7 & (x)) << 4 | (y) << 3 | (u32)(z))

///#define P                struct Machine *m, u64 rde, i64 disp, u64 uimm0
#define A                m, rde, disp, uimm0
#define DISPATCH_NOTHING m, 0, 0, 0

#define CASE(OP, CODE) \
  case OP:             \
    CODE;              \
    break

#define CASR(OP, CODE) \
  case OP:             \
    CODE;              \
    return

#define XLAT(x, y) \
  case x:          \
    return y

extern uint8_t mode, reg, rm;
extern uint32_t ea;

#ifdef MIN
#undef MIN
#endif

#ifdef MAX
#undef MAX
#endif

#define ROUNDDOWN(X, K) ((X) & -(K))
#define ROUNDUP(X, K)   (((X) + (K)-1) & -(K))
#define IS2POW(X)       (!((X) & ((X)-1)))
#define ABS(X)          ((X) >= 0 ? (X) : -(X))
#define MIN(X, Y)       ((Y) > (X) ? (X) : (Y))
#define MAX(X, Y)       ((Y) < (X) ? (X) : (Y))
#define ARRAYLEN(A) \
  ((long)((sizeof(A) / sizeof(*(A))) / ((unsigned)!(sizeof(A) % sizeof(*(A))))))

#define Read16(addr) readw86(addr)
#define Read32(addr) readdw86(addr)
#define Write16(addr, val) writew86(addr, val)
#define Write32(addr, val) writedw86(addr, val)

static struct MachineFpu fpu;

u64 Read64(u32 addr) {
    return (u64)readdw86(addr) | ((u64)readdw86(addr + 4) << 32);
}

void Read80(u8* dst, u32 addr) {
    u32 val32;
    u16 val16;
    val32 = readdw86(addr);
    memcpy(dst, &val32, 4);
    val32 = readdw86(addr + 4);
    memcpy(dst + 4, &val32, 4);
    val16 = readw86(addr + 8);
    memcpy(dst + 8, &val16, 2);
}

void Write64(u32 addr, u64 val) {
    writedw86(addr, val);
    writedw86(addr + 4, val >> 32);
}

void Write80(u32 addr, u8* src) {
    writedw86(addr, *(u32*)src);
    writedw86(addr + 4, *(u32*)(src + 4));
    writew86(addr + 8, *(u16*)(src + 8));
}

u8* SerializeLdbl(u8 b[10], double f) {
    int e;
    union DoublePun u = { f };
    e = (u.i >> 52) & 0x7ff;
    if (!e) {
        e = 0;
    }
    else if (e == 0x7ff) {
        e = 0x7fff;
    }
    else {
        e -= 0x3ff;
        e += 0x3fff;
    }
    *(u16*)(b + 8) = e | u.i >> 63 << 15;
    *(u64*)(b) = (u.i & 0x000fffffffffffff) << 11 | (u64)!!u.f << 63;
    return b;
}

double DeserializeLdbl(const u8 b[10]) {
    union DoublePun u;
    u.i = (u64)(MAX(-1023, MIN(1024, (((*(u16*)(b + 8)) & 0x7fff) - 0x3fff))) + 1023)
        << 52 |
        (((*(u64*)(b)) & 0x7fffffffffffffff) + (1 << (11 - 1))) >> 11 |
        (u64)(b[9] >> 7) << 63;
    return u.f;
}

static i16 FpuGetMemoryShort() {
    //u8 b[2];
    //return Read16(Load(fpu.dp, 2, b));
    return Read16(fpu.dp);
}

static void FpuSetMemoryShort(i16 i) {
    //void* p[2];
    //u8 b[2];
    //Write16(BeginStore(, fpu.dp, 2, p, b), i);
    //EndStore(fpu.dp, 2, p, b);
    Write16(fpu.dp, i);
}

static void OpFstcw() {
    FpuSetMemoryShort(fpu.cw);
}

static void OpFldcw() {
    fpu.cw = FpuGetMemoryShort();
}

#ifndef DISABLE_X87

static void OnFpuStackOverflow() {
    fpu.sw |= kFpuSwIe | kFpuSwC1 | kFpuSwSf;
}

static double OnFpuStackUnderflow() {
    fpu.sw |= kFpuSwIe | kFpuSwSf;
    fpu.sw &= ~kFpuSwC1;
    return -NAN;
}

static double St(int i) {
    if (FpuGetTag(i) == kFpuTagEmpty) OnFpuStackUnderflow();
    return *FpuSt(i);
}

static double St0() {
    return St(0);
}

static double St1() {
    return St(1);
}

static double StRm(u64 rde) {
    return St(rm);
}

static void FpuClearRoundup() {
    fpu.sw &= ~kFpuSwC1;
}

static void FpuClearOutOfRangeIndicator() {
    fpu.sw &= ~kFpuSwC2;
}

static void FpuSetSt0(double x) {
    *FpuSt(0) = x;
}

static void FpuSetStRm(u64 rde, double x) {
    *FpuSt(rm) = x;
}

static void FpuSetStPop(int i, double x) {
    *FpuSt(i) = x;
    FpuPop();
}

static void FpuSetStRmPop(u64 rde, double x) {
    FpuSetStPop(rm, x);
}

static i32 FpuGetMemoryInt() {
    //u8 b[4];
    //return Read32(Load(fpu.dp, 4, b));
    return Read32(fpu.dp);
}

static i64 FpuGetMemoryLong() {
    //u8 b[8];
    //return Read64(Load(fpu.dp, 8, b));
    return Read64(fpu.dp);
}

static float FpuGetMemoryFloat() {
    union FloatPun u;
    u.i = FpuGetMemoryInt();
    return u.f;
}

static double FpuGetMemoryDouble() {
    union DoublePun u;
    u.i = FpuGetMemoryLong();
    return u.f;
}

static void FpuSetMemoryInt(i32 i) {
    //void* p[2];
    //u8 b[4];
    //Write32(BeginStore(fpu.dp, 4, p, b), i);
    //EndStore(fpu.dp, 4, p, b);
    Write32(fpu.dp, i);
}

static void FpuSetMemoryLong(i64 i) {
    //void* p[2];
    //u8 b[8];
    //Write64(BeginStore(fpu.dp, 8, p, b), i);
    //EndStore(fpu.dp, 8, p, b);
    Write64(fpu.dp, i);
}

static void FpuSetMemoryFloat(float f) {
    union FloatPun u = { f };
    FpuSetMemoryInt(u.i);
}

static void FpuSetMemoryDouble(double f) {
    union DoublePun u = { f };
    FpuSetMemoryLong(u.i);
}

static double FpuGetMemoryLdbl() {
    u8 b[10];
    Read80(b, fpu.dp);
    return DeserializeLdbl(b);
}

static void FpuSetMemoryLdbl(double f) {
    void* p[2];
    u8 b[10], t[10];
    SerializeLdbl(b, f);
    //memcpy(BeginStore(fpu.dp, 10, p, t), b, 10);
    //EndStore(fpu.dp, 10, p, t);
    Write80(fpu.dp, b);
}

static double f2xm1(double x) {
    return exp2(x) - 1;
}

static double fyl2x(double x, double y) {
    return y * log2(x);
}

static double fyl2xp1(double x, double y) {
    return y * log2(x + 1);
}

static double fscale(double significand, double exponent) {
    if (isunordered(significand, exponent)) return NAN;
    return ldexp(significand, exponent);
}

static double x87remainder(double x, double y, u32* sw,
    double rem(double, double), double rnd(double)) {
    int s;
    long q;
    double r;
    s = 0;
    r = rem(x, y);
    q = rnd(x / y);
    s &= ~kFpuSwC2; /* ty libm */
    if (q & 1) s |= kFpuSwC1;
    if (q & 2) s |= kFpuSwC3;
    if (q & 4) s |= kFpuSwC0;
    if (sw) *sw = s | (*sw & ~(kFpuSwC0 | kFpuSwC1 | kFpuSwC2 | kFpuSwC3));
    return r;
}

static double fprem(double dividend, double modulus, u32* sw) {
    return x87remainder(dividend, modulus, sw, fmod, trunc);
}

static double fprem1(double dividend, double modulus, u32* sw) {
    return x87remainder(dividend, modulus, sw, remainder, rint);
}

static double FpuAdd(double x, double y) {
    if (!isunordered(x, y)) {
        switch (isinf(y) << 1 | isinf(x)) {
        case 0:
            return x + y;
        case 1:
            return x;
        case 2:
            return y;
        case 3:
            if (signbit(x) == signbit(y)) {
                return x;
            }
            else {
                fpu.sw |= kFpuSwIe;
                return copysign(NAN, x);
            }
        default:
            __builtin_unreachable();
        }
    }
    else {
        return NAN;
    }
}

static double FpuSub(double x, double y) {
    if (!isunordered(x, y)) {
        switch (isinf(y) << 1 | isinf(x)) {
        case 0:
            return x - y;
        case 1:
            return -x;
        case 2:
            return y;
        case 3:
            if (signbit(x) == signbit(y)) {
                fpu.sw |= kFpuSwIe;
                return copysign(NAN, x);
            }
            else {
                return y;
            }
        default:
            __builtin_unreachable();
        }
    }
    else {
        return NAN;
    }
}

static double FpuMul(double x, double y) {
    if (!isunordered(x, y)) {
        if (!((isinf(x) && !y) || (isinf(y) && !x))) {
            return x * y;
        }
        else {
            fpu.sw |= kFpuSwIe;
            return -NAN;
        }
    }
    else {
        return NAN;
    }
}

static double FpuDiv(double x, double y) {
    if (!isunordered(x, y)) {
        if (x || y) {
            if (y) {
                return x / y;
            }
            else {
                fpu.sw |= kFpuSwZe;
                return copysign(INFINITY, x);
            }
        }
        else {
            fpu.sw |= kFpuSwIe;
            return copysign(NAN, x);
        }
    }
    else {
        return NAN;
    }
}

static double FpuRound(double x) {
    switch ((fpu.cw & kFpuCwRc) >> 10) {
    case 0:
        return rint(x);
    case 1:
        return floor(x);
    case 2:
        return ceil(x);
    case 3:
        return trunc(x);
    default:
        __builtin_unreachable();
    }
}

static void FpuCompare(double y) {
    double x = St0();
    fpu.sw &= ~(kFpuSwC0 | kFpuSwC1 | kFpuSwC2 | kFpuSwC3);
    if (!isunordered(x, y)) {
        if (x < y) fpu.sw |= kFpuSwC0;
        if (x == y) fpu.sw |= kFpuSwC3;
    }
    else {
        fpu.sw |= kFpuSwC0 | kFpuSwC2 | kFpuSwC3 | kFpuSwIe;
    }
}

static void OpFxam() {
    double x;
    x = *FpuSt(0);
    fpu.sw &= ~(kFpuSwC0 | kFpuSwC1 | kFpuSwC2 | kFpuSwC3);
    if (signbit(x)) fpu.sw |= kFpuSwC1;
    if (FpuGetTag(0) == kFpuTagEmpty) {
        fpu.sw |= kFpuSwC0 | kFpuSwC3;
    }
    else {
        switch (fpclassify(x)) {
        case FP_NAN:
            fpu.sw |= kFpuSwC0;
            break;
        case FP_INFINITE:
            fpu.sw |= kFpuSwC0 | kFpuSwC2;
            break;
        case FP_ZERO:
            fpu.sw |= kFpuSwC3;
            break;
        case FP_SUBNORMAL:
            fpu.sw |= kFpuSwC2 | kFpuSwC3;
            break;
        case FP_NORMAL:
            fpu.sw |= kFpuSwC2;
            break;
        default:
            __builtin_unreachable();
        }
    }
}

static void OpFtst() {
    FpuCompare(0);
}

static void OpFcmovb(u64 rde) {
    //if (GetFlag(m->flags, FLAGS_CF)) {
    if (cf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmove(u64 rde) {
    //if (GetFlag(m->flags, FLAGS_ZF)) {
    if (zf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmovbe(u64 rde) {
    //if (GetFlag(m->flags, FLAGS_CF) || GetFlag(m->flags, FLAGS_ZF)) {
    if (cf || zf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmovu(u64 rde) {
    //if (GetFlag(m->flags, FLAGS_PF)) {
    if (pf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmovnb(u64 rde) {
    //if (!GetFlag(m->flags, FLAGS_CF)) {
    if (!cf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmovne(u64 rde) {
    //if (!GetFlag(m->flags, FLAGS_ZF)) {
    if (!zf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmovnbe(u64 rde) {
    //if (!(GetFlag(m->flags, FLAGS_CF) || GetFlag(m->flags, FLAGS_ZF))) {
    if (!cf || zf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFcmovnu(u64 rde) {
    //if (!GetFlag(m->flags, FLAGS_PF)) {
    if (!pf) {
        FpuSetSt0(StRm(rde));
    }
}

static void OpFchs() {
    FpuSetSt0(-St0());
}

static void OpFabs() {
    FpuSetSt0(fabs(St0()));
}

static void OpF2xm1() {
    FpuSetSt0(f2xm1(St0()));
}

static void OpFyl2x() {
    FpuSetStPop(1, fyl2x(St0(), St1()));
}

static void OpFyl2xp1() {
    FpuSetStPop(1, fyl2xp1(St0(), St1()));
}

static void OpFcos() {
    FpuClearOutOfRangeIndicator();
    FpuSetSt0(cos(St0()));
}

static void OpFsin() {
    FpuClearOutOfRangeIndicator();
    FpuSetSt0(sin(St0()));
}

static void OpFptan() {
    FpuClearOutOfRangeIndicator();
    FpuSetSt0(tan(St0()));
    FpuPush(1);
}

static void OpFsincos() {
    double tsin, tcos;
    FpuClearOutOfRangeIndicator();
    tsin = sin(St0());
    tcos = cos(St0());
    FpuSetSt0(tsin);
    FpuPush(tcos);
}

static void OpFpatan() {
    FpuClearRoundup();
    FpuSetStPop(1, atan2(St1(), St0()));
}

static void OpFcom(u64 rde) {
    FpuCompare(StRm(rde));
}

static void OpFcomp(u64 rde) {
    FpuCompare(StRm(rde));
    FpuPop();
}

static void OpFaddStEst(u64 rde) {
    FpuSetSt0(FpuAdd(St0(), StRm(rde)));
}

static void OpFmulStEst(u64 rde) {
    FpuSetSt0(FpuMul(St0(), StRm(rde)));
}

static void OpFsubStEst(u64 rde) {
    FpuSetSt0(FpuSub(St0(), StRm(rde)));
}

static void OpFsubrStEst(u64 rde) {
    FpuSetSt0(FpuSub(StRm(rde), St0()));
}

static void OpFdivStEst(u64 rde) {
    FpuSetSt0(FpuDiv(St0(), StRm(rde)));
}

static void OpFdivrStEst(u64 rde) {
    FpuSetSt0(FpuDiv(StRm(rde), St0()));
}

static void OpFaddEstSt(u64 rde) {
    FpuSetStRm(rde, FpuAdd(StRm(rde), St0()));
}

static void OpFmulEstSt(u64 rde) {
    FpuSetStRm(rde, FpuMul(StRm(rde), St0()));
}

static void OpFsubEstSt(u64 rde) {
    FpuSetStRm(rde, FpuSub(St0(), StRm(rde)));
}

static void OpFsubrEstSt(u64 rde) {
    FpuSetStRm(rde, FpuSub(StRm(rde), St0()));
}

static void OpFdivEstSt(u64 rde) {
    FpuSetStRm(rde, FpuDiv(StRm(rde), St0()));
}

static void OpFdivrEstSt(u64 rde) {
    FpuSetStRm(rde, FpuDiv(St0(), StRm(rde)));
}

static void OpFaddp(u64 rde) {
    FpuSetStRmPop(rde, FpuAdd(St0(), StRm(rde)));
}

static void OpFmulp(u64 rde) {
    FpuSetStRmPop(rde, FpuMul(St0(), StRm(rde)));
}

static void OpFcompp(u64 rde) {
    OpFcomp(rde);
    FpuPop();
}

static void OpFsubp(u64 rde) {
    FpuSetStRmPop(rde, FpuSub(St0(), StRm(rde)));
}

static void OpFsubrp(u64 rde) {
    FpuSetStPop(1, FpuSub(StRm(rde), St0()));
}

static void OpFdivp(u64 rde) {
    FpuSetStRmPop(rde, FpuDiv(St0(), StRm(rde)));
}

static void OpFdivrp(u64 rde) {
    FpuSetStRmPop(rde, FpuDiv(StRm(rde), St0()));
}

static void OpFadds(u64 rde) {
    FpuSetSt0(FpuAdd(St0(), FpuGetMemoryFloat()));
}

static void OpFmuls(u64 rde) {
    FpuSetSt0(FpuMul(St0(), FpuGetMemoryFloat()));
}

static void OpFcoms() {
    FpuCompare(FpuGetMemoryFloat());
}

static void OpFcomps() {
    OpFcoms();
    FpuPop();
}

static void OpFsubs() {
    FpuSetSt0(FpuSub(St0(), FpuGetMemoryFloat()));
}

static void OpFsubrs() {
    FpuSetSt0(FpuSub(FpuGetMemoryFloat(), St0()));
}

static void OpFdivs() {
    FpuSetSt0(FpuDiv(St0(), FpuGetMemoryFloat()));
}

static void OpFdivrs() {
    FpuSetSt0(FpuDiv(FpuGetMemoryFloat(), St0()));
}

static void OpFaddl() {
    FpuSetSt0(FpuAdd(St0(), FpuGetMemoryDouble()));
}

static void OpFmull() {
    FpuSetSt0(FpuMul(St0(), FpuGetMemoryDouble()));
}

static void OpFcoml() {
    FpuCompare(FpuGetMemoryDouble());
}

static void OpFcompl() {
    FpuCompare(FpuGetMemoryDouble());
    FpuPop();
}

static void OpFsubl() {
    FpuSetSt0(FpuSub(St0(), FpuGetMemoryDouble()));
}

static void OpFsubrl() {
    FpuSetSt0(FpuSub(FpuGetMemoryDouble(), St0()));
}

static void OpFdivl() {
    FpuSetSt0(FpuDiv(St0(), FpuGetMemoryDouble()));
}

static void OpFdivrl() {
    FpuSetSt0(FpuDiv(FpuGetMemoryDouble(), St0()));
}

static void OpFiaddl() {
    FpuSetSt0(FpuAdd(St0(), FpuGetMemoryInt()));
}

static void OpFimull() {
    FpuSetSt0(FpuMul(St0(), FpuGetMemoryInt()));
}

static void OpFicoml() {
    FpuCompare(FpuGetMemoryInt());
}

static void OpFicompl() {
    OpFicoml();
    FpuPop();
}

static void OpFisubl() {
    FpuSetSt0(FpuSub(St0(), FpuGetMemoryInt()));
}

static void OpFisubrl() {
    FpuSetSt0(FpuSub(FpuGetMemoryInt(), St0()));
}

static void OpFidivl() {
    FpuSetSt0(FpuDiv(St0(), FpuGetMemoryInt()));
}

static void OpFidivrl() {
    FpuSetSt0(FpuDiv(FpuGetMemoryInt(), St0()));
}

static void OpFiadds() {
    FpuSetSt0(FpuAdd(St0(), FpuGetMemoryShort()));
}

static void OpFimuls() {
    FpuSetSt0(FpuMul(St0(), FpuGetMemoryShort()));
}

static void OpFicoms() {
    FpuCompare(FpuGetMemoryShort());
}

static void OpFicomps() {
    OpFicoms();
    FpuPop();
}

static void OpFisubs() {
    FpuSetSt0(FpuSub(St0(), FpuGetMemoryShort()));
}

static void OpFisubrs() {
    FpuSetSt0(FpuSub(FpuGetMemoryShort(), St0()));
}

static void OpFidivs() {
    FpuSetSt0(FpuDiv(St0(), FpuGetMemoryShort()));
}

static void OpFidivrs() {
    FpuSetSt0(FpuDiv(FpuGetMemoryShort(), St0()));
}

static void OpFsqrt() {
    FpuClearRoundup();
    FpuSetSt0(sqrt(St0()));
}

static void OpFrndint() {
    FpuSetSt0(FpuRound(St0()));
}

static void OpFscale() {
    FpuClearRoundup();
    FpuSetSt0(fscale(St0(), St1()));
}

static void OpFprem() {
    FpuSetSt0(fprem(St0(), St1(), &fpu.sw));
}

static void OpFprem1() {
    FpuSetSt0(fprem1(St0(), St1(), &fpu.sw));
}

static void OpFdecstp() {
    fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw - (1 << 11)) & kFpuSwSp);
}

static void OpFincstp() {
    fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw + (1 << 11)) & kFpuSwSp);
}

static void OpFxtract() {
    double x = St0();
    FpuSetSt0(logb(x));
    FpuPush(ldexp(x, -ilogb(x)));
}

static void OpFld(u64 rde) {
    FpuPush(StRm(rde));
}

static void OpFlds() {
    FpuPush(FpuGetMemoryFloat());
}

static void OpFsts() {
    FpuSetMemoryFloat(St0());
}

static void OpFstps() {
    OpFsts();
    FpuPop();
}

static void OpFstpt() {
    FpuSetMemoryLdbl(FpuPop());
}

static void OpFstl() {
    FpuSetMemoryDouble(St0());
}

static void OpFstpl() {
    OpFstl();
    FpuPop();
}

static void OpFst(u64 rde) {
    FpuSetStRm(rde, St0());
}

static void OpFstp(u64 rde) {
    FpuSetStRmPop(rde, St0());
}

static void OpFxch(u64 rde) {
    double t = StRm(rde);
    FpuSetStRm(rde, St0());
    FpuSetSt0(t);
}

static void OpFldt() {
    FpuPush(FpuGetMemoryLdbl());
}

static void OpFldl() {
    FpuPush(FpuGetMemoryDouble());
}

static double Fld1(void) {
    return 1;
}

static double Fldl2t(void) {
    return 0xd.49a784bcd1b8afep-2L; /* log₂10 */
}

static double Fldl2e(void) {
    return 0xb.8aa3b295c17f0bcp-3L; /* log₂𝑒 */
}

static double Fldpi(void) {
    return 0x1.921fb54442d1846ap+1L; /* π */
}

static double Fldlg2(void) {
    return 0x9.a209a84fbcff799p-5L; /* log₁₀2 */
}

static double Fldln2(void) {
    return 0xb.17217f7d1cf79acp-4L; /* logₑ2 */
}

static double Fldz(void) {
    return 0;
}

static void OpFldConstant(u64 rde) {
    double x;
    switch (rm) {
        CASE(0, x = Fld1());
        CASE(1, x = Fldl2t());
        CASE(2, x = Fldl2e());
        CASE(3, x = Fldpi());
        CASE(4, x = Fldlg2());
        CASE(5, x = Fldln2());
        CASE(6, x = Fldz());
    default:
        ///OpUdImpl();
    }
    FpuPush(x);
}

static void OpFilds() {
    FpuPush(FpuGetMemoryShort());
}

static void OpFildl() {
    FpuPush(FpuGetMemoryInt());
}

static void OpFildll() {
    FpuPush(FpuGetMemoryLong());
}

static void OpFisttpl() {
    FpuSetMemoryInt(FpuPop());
}

static void OpFisttpll() {
    FpuSetMemoryLong(FpuPop());
}

static void OpFisttps() {
    FpuSetMemoryShort(FpuPop());
}

static void OpFists() {
    FpuSetMemoryShort(FpuRound(St0()));
}

static void OpFistl() {
    FpuSetMemoryInt(FpuRound(St0()));
}

static void OpFistll() {
    FpuSetMemoryLong(FpuRound(St0()));
}

static void OpFistpl() {
    OpFistl();
    FpuPop();
}

static void OpFistpll() {
    OpFistll();
    FpuPop();
}

static void OpFistps() {
    OpFists();
    FpuPop();
}

static void OpFcomi(u64 rde) {
    double x, y;
    x = St0();
    y = StRm(rde);
    if (!isunordered(x, y)) {
        //m->flags = SetFlag(m->flags, FLAGS_ZF, x == y);
        //m->flags = SetFlag(m->flags, FLAGS_CF, x < y);
        //m->flags = SetFlag(m->flags, FLAGS_PF, false);
        zf = (x == y) ? 1 : 0;
        cf = (x < y) ? 1 : 0;
        pf = 0;
    }
    else {
        fpu.sw |= kFpuSwIe;
        //m->flags = SetFlag(m->flags, FLAGS_ZF, true);
        //m->flags = SetFlag(m->flags, FLAGS_CF, true);
        //m->flags = SetFlag(m->flags, FLAGS_PF, true);
        zf = 1;
        cf = 1;
        pf = 1;
    }
}

static void OpFucom(u64 rde) {
    FpuCompare(StRm(rde));
}

static void OpFucomp(u64 rde) {
    FpuCompare(StRm(rde));
    FpuPop();
}

static void OpFcomip(u64 rde) {
    OpFcomi(rde);
    FpuPop();
}

static void OpFucomi(u64 rde) {
    OpFcomi(rde);
}

static void OpFucomip(u64 rde) {
    OpFcomip(rde);
}

static void OpFfree(u64 rde) {
    FpuSetTag(rm, kFpuTagEmpty);
}

static void OpFfreep(u64 rde) {
    if (rm) OpFfree(rde);
    FpuPop();
}

static void OpFstswMw() {
    FpuSetMemoryShort(fpu.sw);
}

static void OpFstswAx() {
    //Write16(m->ax, fpu.sw);
    CPU_AX = fpu.sw;
}

static void SetFpuEnv(u8 p[28]) {
    *(u16*)(p + 0) = fpu.cw;
    *(u16*)(p + 4) = fpu.sw;
    *(u16*)(p + 8) = fpu.tw;
    *(u64*)(p + 12) = fpu.ip64;
    *(u16*)(p + 18) = fpu.op;
    *(u64*)(p + 20) = fpu.dp;
}

static void GetFpuEnv(u8 p[28]) {
    fpu.cw = *(u16*)(p + 0);
    fpu.sw = *(u16*)(p + 4);
    fpu.tw = *(u16*)(p + 8);
}

static void OpFstenv() {
    void* p[2];
    u8 b[28];
    SetFpuEnv(b); // BeginStore(fpu.dp, sizeof(b), p, b));
    //EndStore(fpu.dp, sizeof(b), p, b);
}

static void OpFldenv() {
    u8 b[28];
    GetFpuEnv(b); //Load(fpu.dp, sizeof(b), b));
}

static void OpFsave() {
    int i;
    void* p[2];
    u8* a, b[108], t[16];
    a = b; // BeginStore(fpu.dp, sizeof(b), p, b);
    SetFpuEnv(a);
    memset(t, 0, sizeof(t));
    for (i = 0; i < 8; ++i) {
        SerializeLdbl(a + 28 + i * 10, *FpuSt(i));
    }
    //EndStore(fpu.dp, sizeof(b), p, b);
    OpFinit();
}

static void OpFrstor() {
    int i;
    u8* a, b[108];
    a = b; // Load(fpu.dp, sizeof(b), b);
    GetFpuEnv(a);
    for (i = 0; i < 8; ++i) {
        *FpuSt(i) = DeserializeLdbl(a + 28 + i * 10);
    }
}

static void OpFnclex() {
    fpu.sw &= ~(kFpuSwIe | kFpuSwDe | kFpuSwZe | kFpuSwOe | kFpuSwUe |
        kFpuSwPe | kFpuSwEs | kFpuSwSf | kFpuSwBf);
}

static void OpFnop() {
    /* do nothing */
}

void OpFinit() {
    fpu.cw = 0x037f;
    fpu.sw = 0;
    fpu.tw = -1;
}

void OpFwait() {
    int sw, cw;
    sw = fpu.sw;
    cw = fpu.cw;
    if (((sw & kFpuSwIe) && !(cw & kFpuCwIm)) ||
        ((sw & kFpuSwDe) && !(cw & kFpuCwDm)) ||
        ((sw & kFpuSwZe) && !(cw & kFpuCwZm)) ||
        ((sw & kFpuSwOe) && !(cw & kFpuCwOm)) ||
        ((sw & kFpuSwUe) && !(cw & kFpuCwUm)) ||
        ((sw & kFpuSwPe) && !(cw & kFpuCwPm)) ||
        ((sw & kFpuSwSf) && !(cw & kFpuCwIm))) {
        //HaltMachine(kMachineFpuException); //TODO
    }
}

int FpuGetTag(unsigned i) {
    unsigned t;
    t = fpu.tw;
    i += (fpu.sw & kFpuSwSp) >> 11;
    i &= 7;
    i *= 2;
    t &= 3 << i;
    t >>= i;
    return t;
}

void FpuSetTag(unsigned i, unsigned t) {
    i += (fpu.sw & kFpuSwSp) >> 11;
    t &= 3;
    i &= 7;
    i *= 2;
    fpu.tw &= ~(3 << i);
    fpu.tw |= t << i;
}

void FpuPush(double x) {
    if (FpuGetTag(-1) != kFpuTagEmpty) OnFpuStackOverflow();
    fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw - (1 << 11)) & kFpuSwSp);
    *FpuSt(0) = x;
    FpuSetTag(0, kFpuTagValid);
}

double FpuPop() {
    double x;
    if (FpuGetTag(0) != kFpuTagEmpty) {
        x = *FpuSt(0);
        FpuSetTag(0, kFpuTagEmpty);
    }
    else {
        x = OnFpuStackUnderflow();
    }
    fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw + (1 << 11)) & kFpuSwSp);
    return x;
}

void OpFpu(uint8_t opcode) {
    unsigned op;
    u64 rde = 0; //unused
    bool ismemory;
    //printf("FPU op %02X\n", opcode);
    op = opcode & 7; //Opcode(rde) & 7;
    fpu.ip64 = CPU_IP - 1;
    modregrm();
    ismemory = mode != 3;
    fpu.op = op << 8 | mode << 6 | reg << 3 | rm;
    //fpu.dp = ismemory ? ComputeAddress(A) : 0;
    if (ismemory) {
        getea(rm);
        fpu.dp = ea;
    }
    else {
        fpu.dp = 0;
    }
    switch (DISP(op, ismemory, reg)) {
        CASE(DISP(0xD8, FPUREG, 0), OpFaddStEst(rde));
        CASE(DISP(0xD8, FPUREG, 1), OpFmulStEst(rde));
        CASE(DISP(0xD8, FPUREG, 2), OpFcom(rde));
        CASE(DISP(0xD8, FPUREG, 3), OpFcomp(rde));
        CASE(DISP(0xD8, FPUREG, 4), OpFsubStEst(rde));
        CASE(DISP(0xD8, FPUREG, 5), OpFsubrStEst(rde));
        CASE(DISP(0xD8, FPUREG, 6), OpFdivStEst(rde));
        CASE(DISP(0xD8, FPUREG, 7), OpFdivrStEst(rde));
        CASE(DISP(0xD8, MEMORY, 0), OpFadds(rde));
        CASE(DISP(0xD8, MEMORY, 1), OpFmuls(rde));
        CASE(DISP(0xD8, MEMORY, 2), OpFcoms());
        CASE(DISP(0xD8, MEMORY, 3), OpFcomps());
        CASE(DISP(0xD8, MEMORY, 4), OpFsubs());
        CASE(DISP(0xD8, MEMORY, 5), OpFsubrs());
        CASE(DISP(0xD8, MEMORY, 6), OpFdivs());
        CASE(DISP(0xD8, MEMORY, 7), OpFdivrs());
        CASE(DISP(0xD9, FPUREG, 0), OpFld(rde));
        CASE(DISP(0xD9, FPUREG, 1), OpFxch(rde));
        CASE(DISP(0xD9, FPUREG, 2), OpFnop());
        CASE(DISP(0xD9, FPUREG, 3), OpFstp(rde));
        CASE(DISP(0xD9, FPUREG, 5), OpFldConstant(rde));
        CASE(DISP(0xD9, MEMORY, 0), OpFlds());
        CASE(DISP(0xD9, MEMORY, 2), OpFsts());
        CASE(DISP(0xD9, MEMORY, 3), OpFstps());
        CASE(DISP(0xD9, MEMORY, 4), OpFldenv());
        CASE(DISP(0xD9, MEMORY, 5), OpFldcw());
        CASE(DISP(0xD9, MEMORY, 6), OpFstenv());
        CASE(DISP(0xD9, MEMORY, 7), OpFstcw());
        CASE(DISP(0xDA, FPUREG, 0), OpFcmovb(rde));
        CASE(DISP(0xDA, FPUREG, 1), OpFcmove(rde));
        CASE(DISP(0xDA, FPUREG, 2), OpFcmovbe(rde));
        CASE(DISP(0xDA, FPUREG, 3), OpFcmovu(rde));
        CASE(DISP(0xDA, MEMORY, 0), OpFiaddl());
        CASE(DISP(0xDA, MEMORY, 1), OpFimull());
        CASE(DISP(0xDA, MEMORY, 2), OpFicoml());
        CASE(DISP(0xDA, MEMORY, 3), OpFicompl());
        CASE(DISP(0xDA, MEMORY, 4), OpFisubl());
        CASE(DISP(0xDA, MEMORY, 5), OpFisubrl());
        CASE(DISP(0xDA, MEMORY, 6), OpFidivl());
        CASE(DISP(0xDA, MEMORY, 7), OpFidivrl());
        CASE(DISP(0xDB, FPUREG, 0), OpFcmovnb(rde));
        CASE(DISP(0xDB, FPUREG, 1), OpFcmovne(rde));
        CASE(DISP(0xDB, FPUREG, 2), OpFcmovnbe(rde));
        CASE(DISP(0xDB, FPUREG, 3), OpFcmovnu(rde));
        CASE(DISP(0xDB, FPUREG, 5), OpFucomi(rde));
        CASE(DISP(0xDB, FPUREG, 6), OpFcomi(rde));
        CASE(DISP(0xDB, MEMORY, 0), OpFildl());
        CASE(DISP(0xDB, MEMORY, 1), OpFisttpl());
        CASE(DISP(0xDB, MEMORY, 2), OpFistl());
        CASE(DISP(0xDB, MEMORY, 3), OpFistpl());
        CASE(DISP(0xDB, MEMORY, 5), OpFldt());
        CASE(DISP(0xDB, MEMORY, 7), OpFstpt());
        CASE(DISP(0xDC, FPUREG, 0), OpFaddEstSt(rde));
        CASE(DISP(0xDC, FPUREG, 1), OpFmulEstSt(rde));
        CASE(DISP(0xDC, FPUREG, 2), OpFcom(rde));
        CASE(DISP(0xDC, FPUREG, 3), OpFcomp(rde));
        CASE(DISP(0xDC, FPUREG, 4), OpFsubEstSt(rde));
        CASE(DISP(0xDC, FPUREG, 5), OpFsubrEstSt(rde));
        CASE(DISP(0xDC, FPUREG, 6), OpFdivEstSt(rde));
        CASE(DISP(0xDC, FPUREG, 7), OpFdivrEstSt(rde));
        CASE(DISP(0xDC, MEMORY, 0), OpFaddl());
        CASE(DISP(0xDC, MEMORY, 1), OpFmull());
        CASE(DISP(0xDC, MEMORY, 2), OpFcoml());
        CASE(DISP(0xDC, MEMORY, 3), OpFcompl());
        CASE(DISP(0xDC, MEMORY, 4), OpFsubl());
        CASE(DISP(0xDC, MEMORY, 5), OpFsubrl());
        CASE(DISP(0xDC, MEMORY, 6), OpFdivl());
        CASE(DISP(0xDC, MEMORY, 7), OpFdivrl());
        CASE(DISP(0xDD, FPUREG, 0), OpFfree(rde));
        CASE(DISP(0xDD, FPUREG, 1), OpFxch(rde));
        CASE(DISP(0xDD, FPUREG, 2), OpFst(rde));
        CASE(DISP(0xDD, FPUREG, 3), OpFstp(rde));
        CASE(DISP(0xDD, FPUREG, 4), OpFucom(rde));
        CASE(DISP(0xDD, FPUREG, 5), OpFucomp(rde));
        CASE(DISP(0xDD, MEMORY, 0), OpFldl());
        CASE(DISP(0xDD, MEMORY, 1), OpFisttpll());
        CASE(DISP(0xDD, MEMORY, 2), OpFstl());
        CASE(DISP(0xDD, MEMORY, 3), OpFstpl());
        CASE(DISP(0xDD, MEMORY, 4), OpFrstor());
        CASE(DISP(0xDD, MEMORY, 6), OpFsave());
        CASE(DISP(0xDD, MEMORY, 7), OpFstswMw());
        CASE(DISP(0xDE, FPUREG, 0), OpFaddp(rde));
        CASE(DISP(0xDE, FPUREG, 1), OpFmulp(rde));
        CASE(DISP(0xDE, FPUREG, 2), OpFcomp(rde));
        CASE(DISP(0xDE, FPUREG, 3), OpFcompp(rde));
        CASE(DISP(0xDE, FPUREG, 4), OpFsubp(rde));
        CASE(DISP(0xDE, FPUREG, 5), OpFsubrp(rde));
        CASE(DISP(0xDE, FPUREG, 6), OpFdivp(rde));
        CASE(DISP(0xDE, FPUREG, 7), OpFdivrp(rde));
        CASE(DISP(0xDE, MEMORY, 0), OpFiadds());
        CASE(DISP(0xDE, MEMORY, 1), OpFimuls());
        CASE(DISP(0xDE, MEMORY, 2), OpFicoms());
        CASE(DISP(0xDE, MEMORY, 3), OpFicomps());
        CASE(DISP(0xDE, MEMORY, 4), OpFisubs());
        CASE(DISP(0xDE, MEMORY, 5), OpFisubrs());
        CASE(DISP(0xDE, MEMORY, 6), OpFidivs());
        CASE(DISP(0xDE, MEMORY, 7), OpFidivrs());
        CASE(DISP(0xDF, FPUREG, 0), OpFfreep(rde));
        CASE(DISP(0xDF, FPUREG, 1), OpFxch(rde));
        CASE(DISP(0xDF, FPUREG, 2), OpFstp(rde));
        CASE(DISP(0xDF, FPUREG, 3), OpFstp(rde));
        CASE(DISP(0xDF, FPUREG, 4), OpFstswAx());
        CASE(DISP(0xDF, FPUREG, 5), OpFucomip(rde));
        CASE(DISP(0xDF, FPUREG, 6), OpFcomip(rde));
        CASE(DISP(0xDF, MEMORY, 0), OpFilds());
        CASE(DISP(0xDF, MEMORY, 1), OpFisttps());
        CASE(DISP(0xDF, MEMORY, 2), OpFists());
        CASE(DISP(0xDF, MEMORY, 3), OpFistps());
        CASE(DISP(0xDF, MEMORY, 5), OpFildll());
        CASE(DISP(0xDF, MEMORY, 7), OpFistpll());
    case DISP(0xD9, FPUREG, 4):
        switch (rm) {
            CASE(0, OpFchs());
            CASE(1, OpFabs());
            CASE(4, OpFtst());
            CASE(5, OpFxam());
        default:
          ///  OpUdImpl();
        }
        break;
    case DISP(0xD9, FPUREG, 6):
        switch (rm) {
            CASE(0, OpF2xm1());
            CASE(1, OpFyl2x());
            CASE(2, OpFptan());
            CASE(3, OpFpatan());
            CASE(4, OpFxtract());
            CASE(5, OpFprem1());
            CASE(6, OpFdecstp());
            CASE(7, OpFincstp());
        default:
            __builtin_unreachable();
        }
        break;
    case DISP(0xD9, FPUREG, 7):
        switch (rm) {
            CASE(0, OpFprem());
            CASE(1, OpFyl2xp1());
            CASE(2, OpFsqrt());
            CASE(3, OpFsincos());
            CASE(4, OpFrndint());
            CASE(5, OpFscale());
            CASE(6, OpFsin());
            CASE(7, OpFcos());
        default:
            __builtin_unreachable();
        }
        break;
    case DISP(0xDb, FPUREG, 4):
        switch (rm) {
            CASE(2, OpFnclex());
            CASE(3, OpFinit());
        default:
           /// OpUdImpl();
        }
        break;
    default:
     ///   OpUdImpl();
    }
}

#else /* DISABLE_X87 */

void(OpFpu)(P) {
    unsigned op;
    bool ismemory;
    op = Opcode(rde) & 7;
    ismemory = ModrmMod(rde) != 3;
    fpu.dp = ismemory ? ComputeAddress(A) : 0;
    switch (DISP(op, ismemory, reg)) {
        CASE(DISP(0xD9, MEMORY, 5), OpFldcw());
        CASE(DISP(0xD9, MEMORY, 7), OpFstcw());
    default:
        OpUdImpl();
    }
}

#endif /* DISABLE_X87 */
