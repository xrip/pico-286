#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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

#define regal 0
#define regah 1
#define regcl 2
#define regch 3
#define regdl 4
#define regdh 5
#define regbl 6
#define regbh 7


#define StepIP(x)  ip += x

#define getmem8(x, y) read86(segbase(x) + (y))
#define getmem16(x, y)  readw86(segbase(x) + (y))
#define putmem8(x, y, z)  write86(segbase(x) + (y), z)
#define putmem16(x, y, z) writew86(segbase(x) + (y), z)
#define signext(value)  (int16_t)(int8_t)(value)
#define signext32(value)  (int32_t)(int16_t)(value)
#define getreg16(regid) wordregs[regid]
#define getreg8(regid)  byteregs[byteregtable[regid]]
#define putreg16(regid, writeval) wordregs[regid] = writeval
#define putreg8(regid, writeval)  byteregs[byteregtable[regid]] = writeval
#define getsegreg(regid)  segregs[regid]
#define putsegreg(regid, writeval)  segregs[regid] = writeval
#define segbase(x)  ((uint32_t) (x) << 4)


#define CPU_FL_CF    cf
#define CPU_FL_PF    pf
#define CPU_FL_AF    af
#define CPU_FL_ZF    zf
#define CPU_FL_SF    sf
#define CPU_FL_TF    tf
#define CPU_FL_IFL    ifl
#define CPU_FL_DF    df
#define CPU_FL_OF    of

#define CPU_CS        segregs[regcs]
#define CPU_DS        segregs[regds]
#define CPU_ES        segregs[reges]
#define CPU_SS        segregs[regss]

#define CPU_AX    wordregs[regax]
#define CPU_BX    wordregs[regbx]
#define CPU_CX    wordregs[regcx]
#define CPU_DX    wordregs[regdx]
#define CPU_SI    wordregs[regsi]
#define CPU_DI    wordregs[regdi]
#define CPU_BP    wordregs[regbp]
#define CPU_SP    wordregs[regsp]
#define CPU_IP        ip

#define CPU_AL    byteregs[regal]
#define CPU_BL    byteregs[regbl]
#define CPU_CL    byteregs[regcl]
#define CPU_DL    byteregs[regdl]
#define CPU_AH    byteregs[regah]
#define CPU_BH    byteregs[regbh]
#define CPU_CH    byteregs[regch]
#define CPU_DH    byteregs[regdh]

