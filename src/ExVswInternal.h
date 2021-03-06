// VardataString writing functions (for uedai's Dlls)

#pragma once

#include <vector>
#include <string>
#include "ExVardataString.h"

extern void WINAPI knowbugVsw_addValueInt(vswriter_t, char const* name, void const* ptr);

#ifdef with_ExtraBasics
extern void WINAPI knowbugVsw_addValueBool(vswriter_t, char const* name, void const* ptr);
extern void WINAPI knowbugVsw_addValueSChar(vswriter_t, char const* name, void const* ptr);
extern void WINAPI knowbugVsw_addValueSShort(vswriter_t, char const* name, void const* ptr);
extern void WINAPI knowbugVsw_addValueUShort(vswriter_t, char const* name, void const* ptr);
extern void WINAPI knowbugVsw_addValueUInt(vswriter_t, char const* name, void const* ptr);
extern void WINAPI knowbugVsw_addValueSLong(vswriter_t, char const* name, void const* ptr);
extern void WINAPI knowbugVsw_addValueULong(vswriter_t, char const* name, void const* ptr);
#endif
#ifdef with_Modcmd
extern void WINAPI knowbugVsw_addValueModcmd(vswriter_t, char const* name, void const* ptr);
#endif

struct VswInfoForInternal
{
	string vtname;
	addVarUserdef_t addVar;
	addValueUserdef_t addValue;
};

extern auto vswInfoForInternal() -> std::vector<VswInfoForInternal> const&;
extern void sendVswMethods(HMODULE hDll);
