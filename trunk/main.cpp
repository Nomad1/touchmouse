#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma data_seg("Shared")

static DWORD g_processId = 0;

#pragma data_seg()
#pragma comment(linker, "/section:Shared,rws")

/* WINAPI defines for VC 6.0 */

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
#define MOUSEEVENTF_FROMTOUCH			0xFF515700

#define POINTER_MESSAGE_FLAG_PRIMARY 0x00002000

#define GET_POINTERID_WPARAM(wParam) (LOWORD (wParam))
#define IS_POINTER_FLAG_SET_WPARAM(wParam, flag) (((DWORD)HIWORD (wParam) &(flag)) == (flag))
#define IS_POINTER_PRIMARY_WPARAM(wParam) IS_POINTER_FLAG_SET_WPARAM (wParam, POINTER_MESSAGE_FLAG_PRIMARY)

typedef BOOL (WINAPI * REGISTERTOUCHWINDOW_T)(HWND,LONG);

static REGISTERTOUCHWINDOW_T RegisterTouchWindow = NULL;

#define LOAD_REGISTERTOUCH
#endif

static const char * LogFile = "touch.log";

//#define EXTENDED_LOG

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

static HINSTANCE g_dllInstance = NULL;
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

BOOL WINAPI DllMain( HINSTANCE hinstDll, DWORD fdwReason, PVOID fImpLoad )
{

#ifdef LOAD_REGISTERTOUCH
	if (RegisterTouchWindow == NULL)
		RegisterTouchWindow = (REGISTERTOUCHWINDOW_T)GetProcAddress(LoadLibraryA("user32.dll"), "RegisterTouchWindow");
#endif

#ifdef LOAD_GETPROCESSID
	if (GetProcessId == NULL)
		GetProcessId = (GETPROCESSID_T)GetProcAddress(LoadLibraryA("kernel32.dll"), "GetProcessId");
#endif

	switch( fdwReason )
	{
	case DLL_PROCESS_ATTACH:
		{
			char buffer[MAX_PATH];
			DWORD length = GetModuleFileNameA(NULL, buffer, MAX_PATH);
			DWORD processId = GetCurrentProcessId();
			if (length > 0)
			{
				buffer[length] = 0;
				logPrint("Got process module name %s, process id %i\r\n", buffer, processId);
			}
			else
			{
				logPrint("Got empty process module name, id %i\r\n", processId);
			}

			g_dllInstance = hinstDll;
			if (g_processId == 0)
			{
				g_hook = SetWindowsHookEx( WH_CBT, ShellProc, g_dllInstance, 0 );
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
		if (g_dllInstance)
		{
			if (g_hook)
			{
				UnhookWindowsHookEx( g_hook );

				logPrint("Hook released\r\n");
				
				g_hook = NULL;
			}
			g_dllInstance = NULL;
		}
		break;
	}

	return TRUE;
}

LRESULT CALLBACK CustomProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
#if EXTENDED_LOG
	logPrint("Processing custom proc for window %i, uMsg: %i\n", hWnd, uMsg);	
#endif
	static int pointers[2] = {0,0};

	switch (uMsg)
	{
		case WM_POINTERDOWN:
			{
				if (IS_POINTER_PRIMARY_WPARAM(wParam))
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
					int pointer = GET_POINTERID_WPARAM(wParam);

					mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);

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
#if EXTENDED_LOG
					logPrint("Non primary pointer pos x: %i, y: %i, type %X\n", ((int)(short)LOWORD(lParam)), ((int)(short)HIWORD(lParam)), wParam);
#endif
					if (!found)
					{
						logPrint("Calling ESC key\n");
						keybd_event(VK_ESCAPE, 1, 0, 0);
						keybd_event(VK_ESCAPE, 1, KEYEVENTF_KEYUP, 0);
					}

				}
			}
			return S_FALSE;
		case WM_POINTERUP:
			{
				if (IS_POINTER_PRIMARY_WPARAM(wParam))
				{
					mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);
				}
				else
				{
					mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, MOUSEEVENTF_FROMTOUCH);

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
			}
			return S_FALSE;
		case WM_POINTERUPDATE:
			{
				POINT last;
				GetCursorPos(&last);
				int x = ((int)(short)LOWORD(lParam)); 
				int y = ((int)(short)HIWORD(lParam)); 
			
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
#if EXTENDED_LOG
			logPrint("WM message %X, wparam %X, lparam %X\n", uMsg, wParam, lParam);
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
	DWORD processId = GetCurrentProcessId();

#if EXTENDED_LOG
	logPrint("Got shell proc %i, process id %i, our process id %i\n", nCode, processId, g_processId);
#endif

	if (processId == g_processId)
	{
		switch(nCode)
		{
		case HCBT_CREATEWND: // just for logging purposes
			{
				HWND hWnd = (HWND)wParam;
				LPCREATESTRUCT createStruct = ((CBT_CREATEWND*)lParam)->lpcs;

				logPrint("Got object name %s, hwnd %i, style %X\n", createStruct->lpszName, hWnd, createStruct->style);
			}
			break;
			
		case HCBT_ACTIVATE: // going to activate window
			{
				HWND hWnd = (HWND)wParam;

				logPrint("Got activate for hwnd %i\n", hWnd);

				WNDPROC oldWindowProc = (WNDPROC)GetWindowLong(hWnd, GWL_WNDPROC);

				if (oldWindowProc != CustomProc)
				{
					SetWindowLong(hWnd, GWL_WNDPROC, (LPARAM)CustomProc);
					logPrint("Hook set, oldproc %i\n", oldWindowProc);
					SetProp(hWnd, "PROP_PROC", oldWindowProc);
					
					ShowCursor(true);

					if (RegisterTouchWindow)
						RegisterTouchWindow(hWnd, TWF_WANTPALM);

					const char * atom = "MicrosoftTabletPenServiceProperty";
					ATOM id = GlobalAddAtom(atom);
					if (id)
						SetProp(hWnd, atom, (HANDLE)1);
				}
			}
			break;
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
/*

bool InjectCode(LPSECURITY_ATTRIBUTES securityAttributes, HANDLE hProcess, const char * dllPath)
{
	int dllNameSize = MAX_PATH;

	logPrint("Using dll at %s\r\n", dllPath);

	LPVOID data = VirtualAllocEx(hProcess, NULL, dllNameSize, MEM_COMMIT, PAGE_READWRITE );

	if (!data)
	{
		logPrint("Failed to allocate virtual memory\r\n");
		return false;
	}

	if (!WriteProcessMemory( hProcess, data, (void*)dllPath, dllNameSize, NULL ))
	{
		logPrint("Failed to write virtual memory\r\n");
		VirtualFreeEx( hProcess, data, dllNameSize, MEM_RELEASE);
		return false;
	}

	HANDLE hThread = CreateRemoteThread( hProcess, securityAttributes, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, data, 0, NULL );

	bool result = true;

	if (hThread == NULL)
	{
		logPrint("Failed to create thread\r\n");
		result = false;
	}
	else
	{
		WaitForSingleObject( hThread, INFINITE );
		
		DWORD code;
		GetExitCodeThread(hThread, &code);

		CloseHandle(hThread);

		if (code == 0)
		{
			logPrint("Failed to load\r\n");
			result = false;
		} else
			logPrint("Dll loaded\n");
	}

	VirtualFreeEx( hProcess, data, dllNameSize, MEM_RELEASE);

	return result;
}
*/
void _stdcall CALLBACK Start(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
               int nCmdShow)
{
	STARTUPINFOA startupInfo;
	memset(&startupInfo, 0, sizeof(STARTUPINFOA));

	PROCESS_INFORMATION processInfo;
	memset(&processInfo, 0, sizeof(PROCESS_INFORMATION));

	SECURITY_ATTRIBUTES securityAttributes;
	memset(&securityAttributes, 0, sizeof(SECURITY_ATTRIBUTES));
	securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	securityAttributes.bInheritHandle = TRUE;

	logPrint("Going to start %s\n", lpszCmdLine);
	if (CreateProcessA(NULL, lpszCmdLine, &securityAttributes, NULL, TRUE, 0, NULL, NULL, &startupInfo, &processInfo))
	{
		g_processId = GetProcessId(processInfo.hProcess);
		//InjectCode(&securityAttributes, processInfo.hProcess, "TouchMouse.dll");

		WaitForInputIdle(processInfo.hProcess, INFINITE);
		logPrint("Process started with id %i\n", g_processId);
	} else
		logPrint("Failed to start %s: %i\r\n", lpszCmdLine, GetLastError());
}

