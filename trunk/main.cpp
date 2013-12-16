#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const char * LogFile = "touch.log";

//#define EXTENDED_LOG
//#define USE_ASILOADER


#pragma data_seg("Shared")

static DWORD g_processId = 0;
static bool g_hookMessages = false;

#pragma data_seg()
#pragma comment(linker, "/section:Shared,rws")

#define MOUSEEVENTF_FROMTOUCH			0xFF515700

/* WINAPI defines for VC 6.0 */
#if WINVER < 0x500
#if !defined GetProcessId
typedef DWORD (WINAPI * GETPROCESSID_T)(HANDLE);

static GETPROCESSID_T GetProcessId = NULL;

#define LOAD_GETPROCESSID
#endif

#if !defined RegisterTouchWindow

#define TWF_FINETOUCH					(0x00000001)
#define TWF_WANTPALM					(0x00000002)
#define WM_TOUCH                        0x0240
#define WM_POINTERUPDATE				0x0245
#define WM_POINTERDOWN					0x0246
#define WM_POINTERUP					0x0247

#define POINTER_MESSAGE_FLAG_PRIMARY 0x00002000
#define POINTER_MESSAGE_FLAG_FIRSTBUTTON  0x00000010
#define POINTER_MESSAGE_FLAG_SECONDBUTTON  0x00000020

#define GET_POINTERID_WPARAM(wParam) (LOWORD (wParam))
#define IS_POINTER_FLAG_SET_WPARAM(wParam, flag) (((DWORD)HIWORD (wParam) &(flag)) == (flag))
#define IS_POINTER_PRIMARY_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_PRIMARY)
#define IS_POINTER_FIRSTBUTTON_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_FIRSTBUTTON)
#define IS_POINTER_SECONDBUTTON_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_SECONDBUTTON)

typedef BOOL (WINAPI * REGISTERTOUCHWINDOW_T)(HWND,LONG);

static REGISTERTOUCHWINDOW_T RegisterTouchWindow = NULL;

#define LOAD_REGISTERTOUCH
#endif
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

const char * stristr(const char *string,	/* String to search. */
					 const char *substring)		/* Substring to try to find in string. */
{
    const char *a, *b;

    b = substring;
    if (*b == 0)
		return string;
    
    for ( ; *string != 0; string++)
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

		char str[1024];

		wvsprintfA(str, format, arg);

		va_end(arg);

		DWORD resultLen = strlen(str);

        WriteFile(file, str, resultLen, &resultLen, NULL);

		CloseHandle(file);
	}
}

LRESULT CALLBACK CustomProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#ifdef EXTENDED_LOG
	logPrint("Processing custom proc for window %i, uMsg: %X, %X\r\n", hWnd, uMsg, wParam);	
#endif
	static int pointers[2] = {0,0};
    static bool releaseSecond = false;

	switch (uMsg)
	{
		case WM_POINTERDOWN:
			{
				if (IS_POINTER_PRIMARY_WPARAM(wParam) && !IS_POINTER_SECONDBUTTON_WPARAM(wParam))
				{
					POINT last;
					GetCursorPos(&last);
					int x = ((int)(short)LOWORD(lParam)); 
					int y = ((int)(short)HIWORD(lParam)); 

					SetCursorPos(x, y);
					mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_MOVE, x - last.x, y - last.y, 0, MOUSEEVENTF_FROMTOUCH);
				}
				else
				{
					mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);

					if (!IS_POINTER_SECONDBUTTON_WPARAM(wParam)) // stylus button pressed
					{
						int pointer = GET_POINTERID_WPARAM(wParam);

						bool found = false;

						for(int i=0;i<2;i++)
						{
							if (pointers[i] == 0)
							{
								pointers[i] = pointer;
								found = true;
								break;
							}
						}
#ifdef EXTENDED_LOG
						logPrint("Non primary pointer pos x: %i, y: %i, type %X\r\n", ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)), wParam);
#endif
						if (!found)
						{
							logPrint("Calling ESC key\r\n");
							keybd_event(VK_ESCAPE, 1, 0, 0);
							keybd_event(VK_ESCAPE, 1, KEYEVENTF_KEYUP, 0);
						}
					} else
                    {
                        logPrint("Second button pressed\r\n");
                        releaseSecond = true;
                    }
				}
			}
			return S_FALSE;
		case WM_POINTERUP:
			{
				if (IS_POINTER_PRIMARY_WPARAM(wParam) && !IS_POINTER_SECONDBUTTON_WPARAM(wParam) && !releaseSecond)
				{
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
				}
				else
				{
					mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
                    if (releaseSecond)
                        logPrint("Second button released\r\n");
                    releaseSecond = false;
				}

				int pointer = GET_POINTERID_WPARAM(wParam);

				for(int i=0;i<2;i++)
				{
					if (pointers[i] == pointer)
					{
						pointers[i] = 0;
						break;
					}
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

				if (IS_POINTER_PRIMARY_WPARAM(wParam))
				{
					SetCursorPos(x, y);
					mouse_event(MOUSEEVENTF_MOVE, x - last.x, y - last.y, 0, MOUSEEVENTF_FROMTOUCH);
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
	WNDPROC oldProc = (WNDPROC)GetProp(hWnd, "PROP_PROC");

	if (oldProc != NULL)
		return CallWindowProc((WNDPROC)oldProc, hWnd, uMsg, wParam, lParam);
		
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
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

				WNDPROC oldWindowProc = (WNDPROC)GetWindowLong(hWnd, GWL_WNDPROC);

				if (oldWindowProc != CustomProc)
				{
					if (g_hookMessages)
					{
						SetWindowLong(hWnd, GWL_WNDPROC, (LPARAM)CustomProc);
						logPrint("Hook set, oldproc %i\r\n", oldWindowProc);
						SetProp(hWnd, "PROP_PROC", oldWindowProc);
					}

					ShowCursor(true);

					if (RegisterTouchWindow)
						RegisterTouchWindow(hWnd, TWF_WANTPALM);

					const char * atom = "MicrosoftTabletPenServiceProperty";
					ATOM id = GlobalAddAtom(atom);
					if (id)
						SetProp(hWnd, atom, (HANDLE)1);

#ifdef USE_TOUCHDISABLE
                    SetTouchDisableProperty(hWnd, true);
#endif
				}
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
        WaitForSingleObject(processInfo.hProcess, INFINITE); // weird as for me.. (

		logPrint("Going to exit from RunDll32.exe\n");
	} else
		logPrint("Failed to start %s: %i\r\n", lpszCmdLine, GetLastError());
}

void _stdcall CALLBACK Start(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
               int nCmdShow)
{
	DoStart(lpszCmdLine, true);
}

void _stdcall CALLBACK StartNoHook(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
               int nCmdShow)
{
	DoStart(lpszCmdLine, false);
}

BOOL WINAPI DllMain( HINSTANCE hinstDll, DWORD fdwReason, PVOID fImpLoad )
{
#ifdef LOAD_GETPROCESSID
	if (GetProcessId == NULL)
		GetProcessId = (GETPROCESSID_T)GetProcAddress(LoadLibraryA("kernel32.dll"), "GetProcessId");
#endif

#ifdef LOAD_REGISTERTOUCH
	if (RegisterTouchWindow == NULL)
		RegisterTouchWindow = (REGISTERTOUCHWINDOW_T)GetProcAddress(LoadLibraryA("user32.dll"), "RegisterTouchWindow");
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

			bool hookAsi = false;
#ifdef USE_ASILOADER
			char dllName[MAX_PATH];
			length = GetModuleFileNameA((HINSTANCE)&__ImageBase, dllName, MAX_PATH);
			if (length > 0)
			{
				dllName[length] = 0;
				logPrint("Got dll module name %s\r\n", dllName);
				if (stristr(dllName, ".asi"))
				{
					logPrint("Hooking ASI Loader dll\r\n");

					//if (g_processId == 0)
					LoadLibrary(dllName); // HACK: increment DLL reference count on first start to make sure it is unloaded only when process exits

					g_processId = processId; // running as ASI Loader module in current process
					hookAsi = true;
				}
			}
			else
			{
				logPrint("Got empty dll module name\r\n");
			}
#endif
			if (g_processId == 0 || hookAsi)
			{
				g_hook = SetWindowsHookEx( WH_CBT, ShellProc, hinstDll, 0 );
				logPrint("Hook inited\r\n");
			} else
			{
				logPrint("Hook skipped\r\n");
			}

			break;
		}

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
		break;
	}

	return TRUE;
}
