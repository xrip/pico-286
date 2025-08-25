#ifndef _FPUNEW_H_
#define _FPUNEW_H_

typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef long i32;
typedef unsigned long u32;
typedef long long i64;
typedef unsigned long long u64;

///#include "cpu.h"

struct MachineFpu {
#ifndef DISABLE_X87
    double st[8];
    u32 sw;
    int tw;
    int op;
    i64 ip64;
#endif
    u32 cw;
    i64 dp;
};

union FloatPun {
    float f;
    u32 i;
};

union DoublePun {
    double f;
    u64 i;
};

union FloatVectorPun {
    union FloatPun u[4];
    float f[4];
};

union DoubleVectorPun {
    union DoublePun u[2];
    double f[2];
};

#define kFpuTagValid   0
#define kFpuTagZero    1
#define kFpuTagSpecial 2
#define kFpuTagEmpty   3

#define kFpuCwIm 0x0001 /* invalid operation mask */
#define kFpuCwDm 0x0002 /* denormal operand mask */
#define kFpuCwZm 0x0004 /* zero divide mask */
#define kFpuCwOm 0x0008 /* overflow mask */
#define kFpuCwUm 0x0010 /* underflow mask */
#define kFpuCwPm 0x0020 /* precision mask */
#define kFpuCwPc 0x0300 /* precision: 32,∅,64,80 */
#define kFpuCwRc 0x0c00 /* rounding: even,→-∞,→+∞,→0 */

#define kFpuSwIe 0x0001 /* invalid operation */
#define kFpuSwDe 0x0002 /* denormalized operand */
#define kFpuSwZe 0x0004 /* zero divide */
#define kFpuSwOe 0x0008 /* overflow */
#define kFpuSwUe 0x0010 /* underflow */
#define kFpuSwPe 0x0020 /* precision */
#define kFpuSwSf 0x0040 /* stack fault */
#define kFpuSwEs 0x0080 /* exception summary status */
#define kFpuSwC0 0x0100 /* condition 0 */
#define kFpuSwC1 0x0200 /* condition 1 */
#define kFpuSwC2 0x0400 /* condition 2 */
#define kFpuSwSp 0x3800 /* top stack */
#define kFpuSwC3 0x4000 /* condition 3 */
#define kFpuSwBf 0x8000 /* busy flag */

#define kMxcsrIe  0x0001 /* invalid operation flag */
#define kMxcsrDe  0x0002 /* denormal flag */
#define kMxcsrZe  0x0004 /* divide by zero flag */
#define kMxcsrOe  0x0008 /* overflow flag */
#define kMxcsrUe  0x0010 /* underflow flag */
#define kMxcsrPe  0x0020 /* precision flag */
#define kMxcsrDaz 0x0040 /* denormals are zeros */
#define kMxcsrIm  0x0080 /* invalid operation mask */
#define kMxcsrDm  0x0100 /* denormal mask */
#define kMxcsrZm  0x0200 /* divide by zero mask */
#define kMxcsrOm  0x0400 /* overflow mask */
#define kMxcsrUm  0x0800 /* underflow mask */
#define kMxcsrPm  0x1000 /* precision mask */
#define kMxcsrRc  0x6000 /* rounding control */
#define kMxcsrFtz 0x8000 /* flush to zero */

#define FpuSt(i) (fpu.st + (((i) + ((fpu.sw & kFpuSwSp) >> 11)) & 7))

#define kRexbRmMask 000000003600
#define RexbRm(x)   ((x & kRexbRmMask) >> 007)

#define kRexrRegMask 000000000017
#define RexrReg(x)   ((x & kRexrRegMask) >> 000)

#define kOplengthMask 00007400000000000000000
#define Oplength(x)   ((x & kOplengthMask) >> 065)

#define kRegRexbSrmMask 00000170000
#define RexbSrm(x)      ((x & kRegRexbSrmMask) >> 014)

#define Rex(x)        ((x & 000000000020) >> 004)
#define Osz(x)        ((x & 000000000040) >> 005)
#define Asz(x)        ((x & 000010000000) >> 025)
#define Srm(x)        ((x & 000000070000) >> 014)
#define Rexr(x)       ((x & 000000000010) >> 003)
#define Rexw(x)       ((x & 000000000100) >> 006)
#define Rexx(x)       ((x & 000000400000) >> 021)
#define Rexb(x)       ((x & 000000002000) >> 012)
#define Sego(x)       ((x & 000007000000) >> 022)
#define Ymm(x)        ((x & 010000000000) >> 036)
#define RegLog2(x)    ((x & 006000000000) >> 034)
#define ModrmRm(x)    ((x & 000000001600) >> 007)
#define ModrmReg(x)   ((x & 000000000007) >> 000)
#define ModrmSrm(x)   ((x & 000000070000) >> 014)
#define ModrmMod(x)   ((x & 000060000000) >> 026)
#define RexRexr(x)    ((x & 000000000037) >> 000)
#define RexRexb(x)    ((x & 000000007600) >> 007)
#define RexRexbSrm(x) ((x & 000000370000) >> 014)
#define Modrm(x)      (ModrmMod(x) << 6 | ModrmReg(x) << 3 | ModrmRm(x))

#define SibBase(x)  ((x & 00000000000340000000000) >> 040)
#define SibIndex(x) ((x & 00000000003400000000000) >> 043)
#define SibScale(x) ((x & 00000000014000000000000) >> 046)
#define Opcode(x)   ((x & 00000007760000000000000) >> 050)
#define Opmap(x)    ((x & 00000070000000000000000) >> 060)
#define Mopcode(x)  ((x & 00000077760000000000000) >> 050)
#define Rep(x)      ((x & 00000300000000000000000) >> 063)
#define WordLog2(x) ((x & 00030000000000000000000) >> 071)
#define Vreg(x)     ((x & 01700000000000000000000) >> 074)

#define Bite(x)     (~ModrmSrm(x) & 1)
#define RexbBase(x) (Rexb(x) << 3 | SibBase(x))

#define IsByteOp(x)        (~Srm(rde) & 1)
#define SibExists(x)       (ModrmRm(x) == 4)
#define IsModrmRegister(x) (ModrmMod(x) == 3)
#define SibHasIndex(x)     (SibIndex(x) != 4 || Rexx(x))
#define SibHasBase(x)      (SibBase(x) != 5 || ModrmMod(x))
#define SibIsAbsolute(x)   (!SibHasBase(x) && !SibHasIndex(x))
#define IsRipRelative(x)   (Eamode(x) && ModrmRm(x) == 5 && !ModrmMod(x))

double FpuPop();
int FpuGetTag(unsigned);
void FpuPush(double);
void FpuSetTag(unsigned, unsigned);
void OpFinit();
void OpFpu(u8 opcode);
void OpFwait();

#endif
