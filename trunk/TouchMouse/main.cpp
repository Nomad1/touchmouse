#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const char * LogFile = "touch.log";
static const char * OldProcProperty = "TouchMouseOldAddressProperty";
static const char * MSTabletPenProperty = "MicrosoftTabletPenServiceProperty";
//#define EXTENDED_LOG

#define ARCANUM_POINTER_ADDRESS 0x06046AC // NOTE: it can be changed by lots of factors!

#pragma data_seg("Shared")

static DWORD g_processId = 0;
static bool g_hookMessages = false;

#pragma data_seg()
#pragma comment(linker, "/section:Shared,rws")

#define MOUSEEVENTF_FROMTOUCH			0xFF515700

/* WINAPI defines for VC 6.0 */
#if (WINVER < 0x500)
#define LOAD_NEWAPI

// flags for GetModuleHandleExA
#define GET_MODULE_HANDLE_EX_FLAG_PIN                 (0x00000001)
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS        (0x00000004)

// flag for SystemParametersInfo
#define SPI_SETMOUSESPEED         0x0071

// flags for RegisterTouchWindow
#define TWF_FINETOUCH					(0x00000001)
#define TWF_WANTPALM					(0x00000002)

// WM touch messages
#define WM_TOUCH                        0x0240
#define WM_POINTERUPDATE				0x0245
#define WM_POINTERDOWN					0x0246
#define WM_POINTERUP					0x0247

// WM_POINTER* touch messages parser
#define POINTER_MESSAGE_FLAG_PRIMARY 0x00002000
#define POINTER_MESSAGE_FLAG_FIRSTBUTTON  0x00000010
#define POINTER_MESSAGE_FLAG_SECONDBUTTON  0x00000020

#define GET_POINTERID_WPARAM(wParam) (LOWORD (wParam))
#define IS_POINTER_FLAG_SET_WPARAM(wParam, flag) (((DWORD)HIWORD (wParam) &(flag)) == (flag))
#define IS_POINTER_PRIMARY_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_PRIMARY)
#define IS_POINTER_FIRSTBUTTON_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_FIRSTBUTTON)
#define IS_POINTER_SECONDBUTTON_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_SECONDBUTTON)

// API function definitions
typedef BOOL (WINAPI * REGISTERTOUCHWINDOW_T)(HWND,LONG);
typedef DWORD (WINAPI * GETPROCESSID_T)(HANDLE);
typedef BOOL (WINAPI * GETMODULEHANDLEEXA_T)(DWORD,LPCSTR,HMODULE*);

static REGISTERTOUCHWINDOW_T RegisterTouchWindow = NULL;
static GETPROCESSID_T GetProcessId = NULL;
static GETMODULEHANDLEEXA_T GetModuleHandleExA = NULL;

#else

#include <shellapi.h>
#include <propsys.h>
DEFINE_PROPERTYKEY(PKEY_EdgeGesture_DisableTouchWhenFullscreen, 0x32CE38B2, 0x2C9A, 0x41B1, 0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44, 2);
#define USE_TOUCHDISABLE

HRESULT SetTouchDisableProperty(HWND hwnd, BOOL fDisableTouch)
{
    IPropertyStore* pPropStore;
    HRESULT hrReturnValue = SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&pPropStore));
    if (SUCCEEDED(hrReturnValue))
    {
        PROPVARIANT var;
        var.vt = VT_BOOL;
        var.boolVal = fDisableTouch ? VARIANT_TRUE : VARIANT_FALSE;
        hrReturnValue = pPropStore->SetValue(PKEY_EdgeGesture_DisableTouchWhenFullscreen, var);
        pPropStore->Release();
    }
    return hrReturnValue;
}
#endif


EXTERN_C IMAGE_DOS_HEADER __ImageBase;

inline const char * stristr(const char *string,	/* String to search. */
					        const char *substring)		/* Substring to try to find in string. */
{
    const char *a, *b;

    b = substring;
    if (*b == 0)
		return string;
    
    for ( ; *string; string++)
	{
		if (toupper(*string) != toupper(*b))
			continue;

		a = string;
		while (true)
		{
			if (*b == 0)
				return string;
		    if (toupper(*a++) != toupper(*b++))
				break;
	    }
	
		b = substring;
    }
    return NULL;
}
/* Statics */

static HHOOK g_hook = NULL;

static int g_screenX = 0;
static int g_screenY = 0;

static bool g_isArcanum = false;

/* Forward declaration */

LRESULT WINAPI ShellProc( int nCode, WPARAM wParam, LPARAM lParam );
LRESULT CALLBACK CustomProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* logging */

void logPrint(const char * format, ...)
{
	HANDLE file = CreateFileA(LogFile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,  FILE_ATTRIBUTE_NORMAL, NULL);

	if (file && file != INVALID_HANDLE_VALUE)
	{
		va_list arg;
		va_start(arg, format);

		char str[1024] = {0};

        int shift = wsprintfA(str, "[%i.%03i]  ", GetTickCount() / 1000, GetTickCount() % 1000);

		wvsprintfA(str + shift - 1, format, arg);

		va_end(arg);

		DWORD resultLen = strlen(str);

        WriteFile(file, str, resultLen, &resultLen, NULL);

		CloseHandle(file);
	}
}

#if USE_MEMORY_SCAN
DWORD ScanMemory(long long value)
{
    logPrint("Starting memory scan for 0x%08X%08X\r\n", (int)(value >> 32), (int)value);

    MEMORY_BASIC_INFORMATION mbi = {0};
    unsigned char *pAddress   = NULL,
                 *pEndRegion = NULL;

    DWORD   dwProtectionMask    = PAGE_READONLY | PAGE_EXECUTE_WRITECOPY 
                                  | PAGE_READWRITE | PAGE_WRITECOMBINE;

    while( sizeof(mbi) == VirtualQuery(pEndRegion, &mbi, sizeof(mbi)) )
    {
        pAddress = pEndRegion;
        pEndRegion = pEndRegion + mbi.RegionSize;

        if ((mbi.AllocationProtect & dwProtectionMask) && (mbi.State & MEM_COMMIT))
        {
             for (pAddress; pAddress < pEndRegion ; pAddress+=4)
             {
                 if (*(long long*)pAddress == value)
                 {
                     logPrint("Pattern found at 0x%08X\r\n", pAddress);
                     //return (DWORD)pAddress;
                 }
             }
        }
    }
    return 0;
}
#endif

void CorrectPointer(LPPOINT coords)
{
    if (g_isArcanum)
    {
        DWORD * address = (DWORD*)ARCANUM_POINTER_ADDRESS;
        int x = *address;
        int y = *(address+1);

        if (x >= 0 && x < g_screenX && y >= 0 && y < g_screenY)
        {
            coords->x = x;
            coords->y = y;
            SetCursorPos(x, y);
        } else
        {
            logPrint("Mouse memory address points to junk!\r\n");
            g_isArcanum = false;
        }
    }
}

#include <process.h>

void __cdecl SimulateClick(void * args)
{
    logPrint("Calling tap and hold click simulation\r\n");
    Sleep(100);
    keybd_event(VK_MENU, 0, 0, 0);
    Sleep(100);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
    Sleep(100);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
    Sleep(100);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    Sleep(100);
}

LRESULT CALLBACK CustomProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#ifdef EXTENDED_LOG
	logPrint("Processing custom proc for window %i, uMsg: %X, %X\r\n", hWnd, uMsg, wParam);	
#endif
	static short int pointers[5] = {0,0,0,0,0};
    static short int releaseFirst = 0;
    static short int releaseSecond = 0;

    // for tap and hold gesture
    static short int tapAndHoldCoord[2] = {0,0};
    static int tapAndHoldTimer = 0;

	switch (uMsg)
	{
		case WM_POINTERDOWN:
			{
                int touches = 0;
                short int pointer = GET_POINTERID_WPARAM(wParam);
                for(int i = 0; i < 5; i++)
				{
                    touches++;
					if (pointers[i] == 0)
					{
						pointers[i] = pointer;
						break;
					}
				}

                if (IS_POINTER_SECONDBUTTON_WPARAM(wParam)) // stylus button pressed
                    touches = 2;
               
#ifdef EXTENDED_LOG
				logPrint("Touches: %i, pos x: %i, y: %i, flags %X\r\n", touches, ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)), wParam);
#endif
                POINT last;
        		GetCursorPos(&last);
                CorrectPointer(&last);

                switch(touches)
                {
                case 0: // never happens
                    break;
                case 1:
                    {
                        int x = ((int)(short)LOWORD(lParam)); 
					    int y = ((int)(short)HIWORD(lParam)); 
                    
					    mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_MOVE, x - last.x, y - last.y, 0, MOUSEEVENTF_FROMTOUCH);
					    SetCursorPos(x, y);
                        releaseFirst = pointer;

                        // init tap and hold timer
                        tapAndHoldTimer = GetTickCount();
                        tapAndHoldCoord[0] = x;
                        tapAndHoldCoord[1] = y;
                    }
                    break;
                case 3: 
                    logPrint("Three-finger tap detected! Calling ESC key\r\n");
                    keybd_event(VK_ESCAPE, 0, 0, 0);
                    keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
                    break;
                case 4:
                    break; 
                case 5:
#if USE_MEMORY_SCAN
                    {
                        #define MAKELONGLONG(a,b) ((long long)(((long)(a)&0xFFFFFFFF) | (((long long)((long) (b)&0xFFFFFFFF)) << 32)))

                        POINT last;
        				GetCursorPos(&last);
                        ScanMemory(MAKELONGLONG(last.x, last.y));
                    }
#endif
                    break;

                case 2: // second finger or stylus button
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
                    releaseSecond = pointer;
                    break;
                }
			}
			return S_FALSE;
		case WM_POINTERUP:
			{
                short int pointer = GET_POINTERID_WPARAM(wParam);
                
				for(int i=0; i < 5; i++)
				{
					if (pointers[i] == pointer)
					{
						pointers[i] = 0;
						break;
					}
				}

				if (releaseFirst == pointer)
				{
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
                    releaseFirst = 0;

                    if (tapAndHoldTimer && GetTickCount() - tapAndHoldTimer > 500) // pressing at same position for more than 500 ms
                        _beginthread(&SimulateClick, 0, NULL);

                    tapAndHoldTimer = 0; // tap and hold cancelled or completed
				}

                if (releaseSecond == pointer)
				{
					mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
                    releaseSecond = 0;
				}
                
			}
			return S_FALSE;
		case WM_POINTERUPDATE:
			{
				POINT last;
				GetCursorPos(&last);
				int x = ((int)(short)LOWORD(lParam)); 
				int y = ((int)(short)HIWORD(lParam)); 
                
#ifdef EXTENDED_LOG
				logPrint("Got WM_POINTERUPDATE wparam %X, x %i, y %i\r\n", uMsg, x, y);
#endif
                short int pointer = GET_POINTERID_WPARAM(wParam);

				if (IS_POINTER_PRIMARY_WPARAM(wParam))
				{
                    mouse_event(MOUSEEVENTF_MOVE, x - last.x, y - last.y, 0, MOUSEEVENTF_FROMTOUCH);
					SetCursorPos(x, y);
				}

                if (pointer == releaseFirst && tapAndHoldTimer && (abs(x - tapAndHoldCoord[0]) > 4 || abs(y - tapAndHoldCoord[1]) > 4)) // first pointer moved
                {
                    tapAndHoldTimer = GetTickCount(); // reset timer
                    tapAndHoldCoord[0] = x;
                    tapAndHoldCoord[1] = y;
                } 
			}
			return S_FALSE;
		case WM_TOUCH:
			return S_FALSE;
		default:
#ifdef EXTENDED_LOG
			logPrint("WM message %X, wparam %X, lparam %X\r\n", uMsg, wParam, lParam);
#endif
			break;
	}


	WNDPROC oldProc = (WNDPROC)GetProp(hWnd, OldProcProperty);

	if (oldProc != NULL)
		return CallWindowProc((WNDPROC)oldProc, hWnd, uMsg, wParam, lParam);
		
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void HookWindow(HWND hWnd)
{
    WNDPROC oldProc = (WNDPROC)GetProp(hWnd, OldProcProperty);

	if (!oldProc)
	{
		if (g_hookMessages)
		{
			WNDPROC oldWindowProc = (WNDPROC)SetWindowLong(hWnd, GWL_WNDPROC, (LPARAM)CustomProc);
			logPrint("Hook set, oldproc %i\r\n", oldWindowProc);
    		SetProp(hWnd, OldProcProperty, oldWindowProc);
		} else
    		SetProp(hWnd, OldProcProperty, (HANDLE)1); // make sure that below methods would not be called again

		//ShowCursor(true);

		if (RegisterTouchWindow)
			RegisterTouchWindow(hWnd, TWF_WANTPALM);

		if (GlobalAddAtom(MSTabletPenProperty))
			SetProp(hWnd, MSTabletPenProperty, (HANDLE)1);

#ifdef USE_TOUCHDISABLE
        SetTouchDisableProperty(hWnd, true);
#endif
        int mouseAccel[3] = {0,0,0};

    	SystemParametersInfo(SPI_SETMOUSESPEED, 0, (LPVOID)10, 0); // set mouse speed to default value
    	SystemParametersInfo(SPI_SETMOUSE, 0, &mouseAccel, 0); // disable enchanced precision

        g_screenX = GetSystemMetrics(SM_CXSCREEN);
        g_screenY = GetSystemMetrics(SM_CYSCREEN);
        SetCursorPos(g_screenX / 2, g_screenY / 2);
	} else
        logPrint("Window already hooked %i\r\n", hWnd);
}

LRESULT WINAPI ShellProc( int nCode, WPARAM wParam, LPARAM lParam )
{
#ifdef EXTENDED_LOG
	logPrint("Got shell proc %i, process id %i, our process id %i\r\n", nCode, GetCurrentProcessId(), g_processId);
#endif

	if (g_processId && GetCurrentProcessId() == g_processId)
	{
		switch(nCode)
		{
		case HCBT_CREATEWND: // just for logging purposes
			{
				HWND hWnd = (HWND)wParam;
				LPCREATESTRUCT createStruct = ((CBT_CREATEWND*)lParam)->lpcs;

				logPrint("Got object name %s, hwnd %i, style %X\r\n", createStruct->lpszName, hWnd, createStruct->style);
			}
			break;
			
		case HCBT_ACTIVATE: // going to activate window
			{
				HWND hWnd = (HWND)wParam;

				logPrint("Got activate for hwnd %i\r\n", hWnd);

                HookWindow(hWnd);
			}
			break;
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/* Entry points */

void DoStart(LPSTR lpszCmdLine, bool hook)
{
	g_hookMessages = hook;

	STARTUPINFOA startupInfo;
	memset(&startupInfo, 0, sizeof(STARTUPINFOA));

	PROCESS_INFORMATION processInfo;
	memset(&processInfo, 0, sizeof(PROCESS_INFORMATION));

	SECURITY_ATTRIBUTES securityAttributes;
	memset(&securityAttributes, 0, sizeof(SECURITY_ATTRIBUTES));
	securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	securityAttributes.bInheritHandle = TRUE;

	logPrint("Going to start %s\r\n", lpszCmdLine);
	if (CreateProcessA(NULL, lpszCmdLine, &securityAttributes, NULL, TRUE, 0, NULL, NULL, &startupInfo, &processInfo))
	{
		g_processId = processInfo.dwProcessId;

		logPrint("Process started with id %i\r\n", g_processId);

//		WaitForInputIdle(processInfo.hProcess, INFINITE); // Nomad: I got no idea why it worket and then stopped. One more WinAPI mystery
        WaitForSingleObject(processInfo.hProcess, INFINITE); // Make hook ective until process shutdown weird as for me.. (

		logPrint("Going to exit from RunDll32.exe\n");
	} else
		logPrint("Failed to start %s: %i\r\n", lpszCmdLine, GetLastError());
}

void WINAPI CALLBACK Start(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
               int nCmdShow)
{
	DoStart(lpszCmdLine, true);
}

void WINAPI CALLBACK StartNoHook(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
               int nCmdShow)
{
	DoStart(lpszCmdLine, false);
}

BOOL CALLBACK FindTopWindow( HWND handle, LPARAM option )
{
    DWORD windowProcess = 0;
    GetWindowThreadProcessId(handle, &windowProcess);

#if EXTENDED_LOG
    logPrint("Found window %i for process %i\r\n", handle, windowProcess);
#endif
    if (windowProcess == GetCurrentProcessId())
    {
        *((HWND*)option) = handle;
        return FALSE;
    }

    return TRUE;
}

void WINAPI CALLBACK SetProcess(DWORD processId)
{
    g_processId = processId;
    g_hookMessages = true;
    logPrint("ProcessID set to %i\r\n", processId);

    HWND window = 0;
    EnumWindows(FindTopWindow, (LPARAM)&window);

    if (window)
    {
        logPrint("Process already have top-level window, hooking to it\r\n");
        HookWindow(window);
    } else
        logPrint("No top windows found\r\n");
}

BOOL WINAPI DllMain( HINSTANCE hinstDll, DWORD fdwReason, PVOID fImpLoad )
{
#ifdef LOAD_NEWAPI
	if (GetProcessId == NULL)
		GetProcessId = (GETPROCESSID_T)GetProcAddress(LoadLibraryA("kernel32.dll"), "GetProcessId");
	if (RegisterTouchWindow == NULL)
		RegisterTouchWindow = (REGISTERTOUCHWINDOW_T)GetProcAddress(LoadLibraryA("user32.dll"), "RegisterTouchWindow");
	if (GetModuleHandleExA == NULL)
		GetModuleHandleExA = (GETMODULEHANDLEEXA_T)GetProcAddress(LoadLibraryA("kernel32.dll"), "GetModuleHandleExA");
#endif

	switch( fdwReason )
	{
	case DLL_PROCESS_ATTACH:
		{
			char exeName[MAX_PATH];
			DWORD length = GetModuleFileNameA(NULL, exeName, MAX_PATH);
			DWORD processId = GetCurrentProcessId();
			if (length > 0)
			{
				exeName[length] = 0;
				logPrint("Got process module name %s, process id %i, static load: %i\r\n", exeName, processId, fImpLoad);
			}
			else
			{
				logPrint("Got empty process module name, id %i\r\n", processId);
			}

            g_isArcanum = stristr(exeName, "arcanum") != NULL;

			if (g_processId == 0)// stristr(exeName, "rundll32")) // loading from RunDll32
			{
				g_hook = SetWindowsHookEx( WH_CBT, ShellProc, hinstDll, 0 );
				logPrint("Hook inited\r\n");
			} else
			{
				logPrint("Hook skipped\r\n");
			}
		}
        logPrint("DLL loaded\r\n");
    	break;

	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;

	case DLL_PROCESS_DETACH:
		if (g_hook)
		{
			UnhookWindowsHookEx( g_hook );

			logPrint("Hook released\r\n");
				
			g_hook = NULL;

			g_processId = 0;
		}
        logPrint("DLL unloaded\r\n");
		break;
	}

	return TRUE;
}
