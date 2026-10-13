#ifndef LE32_H
#define LE32_H

#include <exec/types.h>

#ifdef __GNUC__
#define ASM
#define REG(r,y) y __asm( # r )
#else 
#define ASM __asm __saveds
#define REG(r,y) register __ ## r y
#endif

ULONG ASM asm_le32(REG(d0, ULONG a));

#endif
