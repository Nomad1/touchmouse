TouchMouse
==========

Injection patch prject to make old games (actually, Arcanum) work with Windows 8 touchscreen and stylus devices.

Usage
-----

Set shortcut command to something like this:

    rundll32.exe TouchMouse.dll,Start Arcanum.exe -no3d 
    
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


  
