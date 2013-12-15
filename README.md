TouchMouse
==========

TouchMouse is a DLL loader designed to patch old games (mostly Arcanum and Fallout 2) to work with Windows 8 touchscreen and stylus devices. Also in WM preprocesssing mode it emulates right click with two-finger gesture and customizable hotkey with 3-finger tap (hardcoded to ESC at this stage).


Usage
-----

There are two injection modes: with Window Messages (WM) pre-processing and without them. Pre-processing messages allows to add gesture recognition and tweak some settings, but it can cause problems with some games. (Actually Fallout 2 at this moment crashes in WM mode).
There are two entry points in project DLL: Start and StartNoHook. Use first one for Arcanum and similar games and second one if you experience crashes and/or weird bugs. 

## Starting in WM preprocessing mode (Arcanum)
Launch from command line or set shortcut command to this:

    rundll32.exe TouchMouse.dll,Start Arcanum.exe -no3d 

## NoHook mode (Fallout 2)
Launch from command line or set shortcut command to this:

    rundll32.exe TouchMouse.dll,StartNoHook Fallout2.exe



Compilation
-----

First version of this project was build with Visual Studio 6.0 (yeah, VS98!). There is no external dependencies, frameworks, non-system DLLs. I'm pretty sure you can upgrade it to some recent Visual Studio with corresponding Platform SDK. It can happen that you'll also need to comment out some defines in main.cpp.


Contribution
-----
Feel free to fork the software to add support for more games, external configs, gestures, etc!


Disable Windows 8 charms
-----
Windows 8 gestures and panels could be very annoying at some point. I'm planning to disable them with PKEY_EdgeGesture_DisableTouchWhenFullscreen at some near future, but right now you can use following registry tweak to disable them:

    [HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\ImmersiveShell\EdgeUI]
    "DisabledEdges"=dword:0000000f
