#include <gba_base.h>

#include "ff.h"
//---------------------------------------------------------------
void Chip_Reset();
u32 IWRAM_CODE Block_Erase(u32 blockAdd);           // 0 ok, 1 timeout
void Chip_Erase();
void FormatNor();
void WriteFlash(u32 address,u8 *buffer,u32 size);
u32 IWRAM_CODE WriteFlash_with32word(u32 address,u8 *buffer,u32 size); // 0 ok, 1 timeout
void Unlock_NOR_PPB(void);
u32 Loadfile2NOR(TCHAR *filename, u32 NORaddress,u16 have_patch,u8 SAVEMODE);
u32 GetFileListFromNor(void);