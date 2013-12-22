#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const char * LogFile = "touch.log";
static const char * IniFile = ".\\TouchMouse.ini";
static const char * OldProcProperty = "TouchMouseOldAddressProperty";
static const char * MSTabletPenProperty = "MicrosoftTabletPenServiceProperty";
//#define EXTENDED_LOG

#pragma data_seg("Shared")

static DWORD g_processId = 0;
static bool g_hookMessages = false;

#pragma data_seg()
#pragma comment(linker, "/section:Shared,rws")

#define MOUSEEVENTF_FROMTOUCH			0xFF515700
#define MAKELONGLONG(a,b) ((long long)(((long)(a)&0xFFFFFFFF) | (((long long)((long) (b)&0xFFFFFFFF)) << 32)))


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
static volatile bool g_calibrating = false;

// memory address of mouse pointer
static DWORD g_mousePointerAddress = 0;

// virtual keys
static int g_twoFingerTap = VK_RBUTTON;
static int g_threeFingerTap = VK_ESCAPE;
static int g_fourFingerTap = 0;
static int g_fiveFingerTap = 0;
static int g_tapAndHold = VK_MENU;
static int g_scanKey = 0;

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

/* configuration */

void loadSettings(const char * iniFile, const char * exeFile)
{
    if (exeFile)
    {
        logPrint("Checking address for exe file %s\r\n", exeFile + 1);
        g_mousePointerAddress = GetPrivateProfileInt("MemoryAddress", exeFile + 1, g_mousePointerAddress, iniFile);

        if (g_mousePointerAddress == 0 && stristr(exeFile, "arcanum.exe")) // TODO: remove this when we start using .ini files for all games!
            g_mousePointerAddress = 0x06046AC; 
    }
    g_scanKey = GetPrivateProfileInt("MemoryAddress", "ScanKey", g_scanKey, iniFile);

    g_twoFingerTap = GetPrivateProfileInt("Gestures", "TwoFingerTap", g_twoFingerTap, iniFile);
    g_threeFingerTap = GetPrivateProfileInt("Gestures", "ThreeFingerTap", g_threeFingerTap, iniFile);
    g_fourFingerTap = GetPrivateProfileInt("Gestures", "FourFingerTap", g_fourFingerTap, iniFile);
    g_fiveFingerTap = GetPrivateProfileInt("Gestures", "FiveFingerTap", g_fiveFingerTap, iniFile);
    g_tapAndHold = GetPrivateProfileInt("Gestures", "TapAndHold", g_tapAndHold, iniFile);

    if (errno == 0x2)
        logPrint("Ini file %s not found\r\n", iniFile);
}

DWORD ScanMemory(long long value)
{
    logPrint("Starting memory scan for 0x%08X%08X, own address is 0x%08X\r\n", (int)(value >> 32), (int)value, &value);

    MEMORY_BASIC_INFORMATION mbi = {0};
    unsigned char *pAddress   = NULL,
                 *pEndRegion = NULL;

    DWORD   dwProtectionMask    = PAGE_READONLY | PAGE_EXECUTE_WRITECOPY 
                                  | PAGE_READWRITE | PAGE_WRITECOMBINE;

    while( sizeof(mbi) == VirtualQuery(pEndRegion, &mbi, sizeof(mbi)) )
    {
        pAddress = pEndRegion;
        pEndRegion = pEndRegion + mbi.RegionSize;

        if (mbi.Protect != PAGE_NOACCESS && mbi.Protect != PAGE_EXECUTE && (mbi.AllocationProtect & dwProtectionMask) && (mbi.State & MEM_COMMIT))
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

void CorrectPointer(LPPOINT coords)
{
    if (g_mousePointerAddress)
    {
        DWORD * address = (DWORD*)g_mousePointerAddress;
        int x = *address;
        int y = *(address+1);

        if (x >= 0 && x < g_screenX && y >= 0 && y < g_screenY)
        {
            if (x != coords->x || y != coords->y)
            {
#if EXTENDED_LOG
                logPrint("Corrected pointer coords to %i,%i from %i,%i\r\n", x,y, coords->x, coords->y);
#endif
                coords->x = x;
                coords->y = y;
                SetCursorPos(x, y);
            }
        } else
        {
            if (x == -1 && y == -1) // not yet inited in Fallout and similar games
                return;

            logPrint("Mouse memory address points to junk: %i, %i!\r\n", x, y);
            g_mousePointerAddress = 0;
        }
    }
}

#include <process.h>

void __cdecl SimulateClick(void * args)
{
    logPrint("Calling tap and hold click simulation with modifier key %x\r\n", g_tapAndHold);
    Sleep(100);

    if (g_tapAndHold == VK_RBUTTON)
    {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
        Sleep(100);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
    } else
    {
        keybd_event(g_tapAndHold, 0, 0, 0);
        Sleep(100);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
        Sleep(100);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
        Sleep(100);
        keybd_event(g_tapAndHold, 0, KEYEVENTF_KEYUP, 0);
        Sleep(100);
    }
}

void __cdecl SendKey(void * args)
{
    UINT code = (UINT)args;
    int scan = MapVirtualKey(code, 0);
    logPrint("Sending key %i, scan code %i\r\n", code, scan);
    Sleep(100);
    keybd_event(code, scan, KEYEVENTF_SCANCODE, 0);
    Sleep(100);
    keybd_event(code, scan, KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE, 0);
}

void __cdecl StartCalibrate(void * args)
{
    POINT last;
    GetCursorPos(&last);
    logPrint("Starting calibrate\r\n");
    g_calibrating = true;
    Sleep(500);
    mouse_event(MOUSEEVENTF_MOVE, -2000, -2000, 0, MOUSEEVENTF_FROMTOUCH);
	SetCursorPos(0, 0);
    Sleep(100);
    mouse_event(MOUSEEVENTF_MOVE, last.x, last.y, 0, MOUSEEVENTF_FROMTOUCH);
    SetCursorPos(last.x, last.y);
    g_calibrating = false;
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
        case WM_KEYUP:
            if (g_scanKey)
            {
                logPrint("Key pressed %x\r\n", wParam);
                if (wParam == g_scanKey)
                {
                    POINT last;
        		    GetCursorPos(&last);
                    ScanMemory(MAKELONGLONG(last.x, last.y));
                }
            }
            break;
		case WM_POINTERDOWN:
			{
                if (g_calibrating)
                    break;

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

                        if (g_tapAndHold) // init tap and hold timer
                        {
                            tapAndHoldTimer = GetTickCount();
                            tapAndHoldCoord[0] = x;
                            tapAndHoldCoord[1] = y;
                        }
                    }
                    break;
                case 3: 
                    logPrint("Three-finger tap detected! Calling 0x%x key\r\n", g_threeFingerTap);
                    if (g_threeFingerTap)
                    {
                        _beginthread(&SendKey, 0, (void*)g_threeFingerTap);
                    }
                    break;
                case 4:
                    logPrint("Four-finger tap detected! Calling 0x%x key\r\n", g_fourFingerTap);
                    if (g_fourFingerTap)
                    {
                        _beginthread(&SendKey, 0, (void*)g_fourFingerTap);
                    }
                    break; 
                case 5:
                    logPrint("Five-finger tap detected! Calling 0x%x key\r\n", g_fiveFingerTap);
                    if (g_fiveFingerTap)
                    {

                        if (g_fiveFingerTap == VK_NONAME)
                            _beginthread(&StartCalibrate, 0, NULL);
                        else
                        {
                            _beginthread(&SendKey, 0, (void*)g_fiveFingerTap);
                        }
                    }
                    break;

                case 2: // second finger or stylus button

                    if (g_twoFingerTap == VK_RBUTTON)
                    {
                        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
                        releaseSecond = pointer;
                    } else
                    {
                        _beginthread(&SendKey, 0, (void*)g_twoFingerTap);
                    }
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
                if (g_calibrating)
                    break;
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
#if EXTENDED_LOG
		case HCBT_CREATEWND: // just for logging purposes
			{
				HWND hWnd = (HWND)wParam;
				LPCREATESTRUCT createStruct = ((CBT_CREATEWND*)lParam)->lpcs;
				logPrint("Got object name %s, hwnd %i, style %X\r\n", createStruct->lpszName, hWnd, createStruct->style);
			}
			break;
#endif			
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

            loadSettings(IniFile, strrchr(exeName, '\\'));

            if (g_mousePointerAddress)
                logPrint("Using mouse pointer address 0x%08X\r\n", g_mousePointerAddress);

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
