#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef void (WINAPI * SETPROCESS_T)(DWORD);

#ifdef NO_CRT

#pragma comment(linker, "/MERGE:.rdata=.text") 

#pragma comment(linker, "/MERGE:.data=.text") 

#pragma comment(linker, "/FILEALIGN:512 /SECTION:.text,ERW /IGNORE:4078 /ignore:4254") 

#pragma comment(linker, "/NODEFAULTLIB") 

#pragma comment(linker, "/SUBSYSTEM:WINDOWS") 

#pragma comment(linker, "/OPT:REF") 

#pragma comment(linker, "/OPT:NOWIN98") 

extern "C"
BOOL WINAPI _DllMainCRTStartup(
        HINSTANCE  hDllHandle,
        DWORD   dwReason,
        LPVOID  lpreserved
        )
#else
BOOL WINAPI DllMain( HINSTANCE hinstDll, DWORD dwReason, PVOID fImpLoad )
#endif
{
	if( dwReason == DLL_PROCESS_ATTACH)
	{
		SETPROCESS_T SetProcess = (SETPROCESS_T)GetProcAddress(LoadLibraryA("TouchMouse.dll"), "SetProcess");

		if (SetProcess)
			SetProcess(GetCurrentProcessId());
	}
	return TRUE;
}
