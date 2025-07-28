#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

extern int a20_enabled;
void init_umb();

#define regax 0
#define regcx 1
#define regdx 2
#define regbx 3
#define regsp 4
#define regbp 5
#define regsi 6
#define regdi 7

#define reges 0
#define regcs 1
#define regss 2
#define regds 3
#define regfs 4
#define reggs 5

// eax
#define regal 0
#define regah 1
// 2, 3

// ecx
#define regcl 4
#define regch 5
// 6, 7

// edx
#define regdl 8
#define regdh 9
// 10, 11

// ebx
#define regbl 12
#define regbh 13
// 14, 14


#define StepIP(x)  ip += x

#define segregs ((uint16_t*)segregs32)
#define getmem8(x, y) read86(segbase(x) + (y))
#define getmem16(x, y)  readw86(segbase(x) + (y))
#define getmem32(x, y)  readdw86(segbase(x) + (y))
#define putmem8(x, y, z)  write86(segbase(x) + (y), z)
#define putmem16(x, y, z) writew86(segbase(x) + (y), z)
#define putmem32(x, y, z) writedw86(segbase(x) + (y), z)
#define signext(value)  (int16_t)(int8_t)(value)
#define signext32(value)  (int32_t)(int16_t)(value)
#define getreg16(regid) wordregs[(regid) << 1]
#define getreg32(regid) dwordregs[regid]
#define getreg8(regid)  byteregs[byteregtable[regid]]
#define putreg16(regid, writeval) wordregs[(regid) << 1] = writeval
#define putreg32(regid, writeval) dwordregs[regid] = writeval
#define putreg8(regid, writeval)  byteregs[byteregtable[regid]] = writeval
#define getsegreg(regid)            segregs[(regid) << 1]
#define putsegreg(regid, writeval)  segregs[(regid) << 1] = writeval
#define segbase(x)  ((uint32_t) (x) << 4)

#define cf  x86_flags.bits.CF
#define pf  x86_flags.bits.PF
#define af  x86_flags.bits.AF
#define zf  x86_flags.bits.ZF
#define sf  x86_flags.bits.SF
#define tf  x86_flags.bits.TF
#define ifl x86_flags.bits.IF
#define df  x86_flags.bits.DF
#define of  x86_flags.bits.OF

#define CPU_FL_CF    cf
#define CPU_FL_PF    pf
#define CPU_FL_AF    af
#define CPU_FL_ZF    zf
#define CPU_FL_SF    sf
#define CPU_FL_TF    tf
#define CPU_FL_IFL   ifl
#define CPU_FL_DF    df
#define CPU_FL_OF    of

#define FLAG_CF_OF_MASK ((1u << 11) | 1)
#define FLAG_CF_AF_MASK ((1u << 4) | 1)

#define CPU_CS    segregs[regcs << 1]
#define CPU_DS    segregs[regds << 1]
#define CPU_ES    segregs[reges << 1]
#define CPU_SS    segregs[regss << 1]
#define CPU_FS    segregs[regfs << 1]
#define CPU_GS    segregs[reggs << 1]

#define CPU_EAX   dwordregs[regax]
#define CPU_EBX   dwordregs[regbx]
#define CPU_ECX   dwordregs[regcx]
#define CPU_EDX   dwordregs[regdx]
#define CPU_ESI   dwordregs[regsi]
#define CPU_EDI   dwordregs[regdi]
#define CPU_EBP   dwordregs[regbp]
#define CPU_ESP   dwordregs[regsp]

#define CPU_AX    wordregs[regax << 1]
#define CPU_BX    wordregs[regbx << 1]
#define CPU_CX    wordregs[regcx << 1]
#define CPU_DX    wordregs[regdx << 1]
#define CPU_SI    wordregs[regsi << 1]
#define CPU_DI    wordregs[regdi << 1]
#define CPU_BP    wordregs[regbp << 1]
#define CPU_SP    wordregs[regsp << 1]
#define CPU_IP    (*(uint16_t*)&ip32)
#define ip        (*(uint16_t*)&ip32)

#define CPU_AL    byteregs[regal]
#define CPU_BL    byteregs[regbl]
#define CPU_CL    byteregs[regcl]
#define CPU_DL    byteregs[regdl]
#define CPU_AH    byteregs[regah]
#define CPU_BH    byteregs[regbh]
#define CPU_CH    byteregs[regch]
#define CPU_DH    byteregs[regdh]

