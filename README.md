TouchMouse
==========

TouchMouse is a DLL loader designed to patch old games (Arcanum, Fallout, ToEE and others) to work with Windows 8 touchscreen and stylus devices. Also in WM preprocesssing mode it emulates right click with two-finger gesture or stylus button and customizable hotkey with 3-finger tap (hardcoded to ESC at this stage).


Installation
-----

Starting with version 0.3.1 TouchMouse is provided in form of .ASI + .DLL files to work with [ASI Loader](https://github.com/ThirteenAG/Ultimate_ASI_Loader/releases). Install the loader from above link (skip this step for Arcanum and ToEE) and copy files from _build_ folder to your game folder and launch the game as usually.

If you don't like ASI Loader idea it is possible to launch the DLL directly from command line:

    rundll32.exe TouchMouse.dll,Start Arcanum.exe -no3d 

If there are crashes on start you could also try NoHook mode (no message queue injection):

    rundll32.exe TouchMouse.dll,StartNoHook Fallout2.exe


Supported Games
-----

Note that there are games that could benefit from TouchMouse even if they don't have issues with pointer position. Some games require you to use re-calibrate command (five-finger tap with default .ini file)

<table>
    <tr>
        <th>Game</th>
        <th>Status</th>
        <th>Need ASI Loader</th>
        <th>Description</th>
    </tr>
    <tr>
        <td><a href="http://www.gog.com/game/arcanum_of_steamworks_and_magick_obscura">Arcanum: Of Steamworks and Magick Obscura</a></td>
        <td>Gold*</td>
        <td>No</td>
        <td>Works perfectly. Two fingers gesture defaults to right click, Three fingers tap calls ESC, Tap&Hold calls Alt-Click (attach door or chest or drag the body)</td>
    </tr>
    <tr>
        <td><a href="http://www.gog.com/game/fallout_2">Fallout 2</a></td>
        <td>Silver*</td>
        <td>Yes</td>
        <td>Sometimes cursor can appear shifted but it is solved on the next tap (only with memory search)  or by re-calibration</td>
    </tr>
    <tr>
        <td><a href="http://www.gog.com/game/fallout">Fallout</a></td>
        <td>Bronze*</td>
        <td>Yes</td>
        <td>Cursor jitters on touch sometimes</td>
    </tr>
    <tr>
        <td><a href="http://www.gog.com/game/the_temple_of_elemental_evil">Temple Of Elemental Evil</a></td>
        <td>Silver</td>
        <td>No</td>
        <td>Game is 100% playable, however you need to re-calibrate from time to time. It is advised to set TapAndHold=2 in .ini file to simulate right click with long tap gesture.</td>
    </tr>
    <tr>
        <td><a href="http://www.gog.com/game/might_and_magic_7_for_blood_and_honor">Might and Magic 7</a></td>
        <td>Garbage</td>
        <td>Yes</td>
        <td>Mouse pointer moves correctly but game is useless without keyboard or on-screen overlay with movement keys (planned in next versions).</td>
    </tr>
    <tr>
        <td><a href="http://www.gog.com/game/might_and_magic_7_for_blood_and_honor">A New Beginning</a></td>
        <td>Bronze-</td>
        <td>Yes</td>
        <td>Pointer works correctly only in windowed mode, however it works in windowed mode even without TouchMouse</td>
    </tr>
</table>

<span>* Game would benefit from memory search feature and correct address in .ini file</span>

Compilation
-----

First version of this project was build with Visual Studio 6.0 (yeah, VS98!). There is no external dependencies, frameworks, non-system DLLs. Also it is compatible with Visual Studio 2012. Corresponding .sln and .vcxxproj files are at the same folder.


Contribution
-----
Feel free to fork the software to add support for more games, external configs, gestures, etc!


Disable Windows 8 charms
-----
Windows 8 gestures and panels could be very annoying at some point. If you compile DLL with VS 2012 (or use pre-compiled DLL from _build_ folder) those are disabled automatically. For VS98 there is no support for this methods and VARIANT types, so you can use following registry tweak to disable them (not tested in real environment!):

    [HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\ImmersiveShell\EdgeUI]
    "DisabledEdges"=dword:0000000f


Mouse speed and "Enhance Pointer Precision"
-----
Non-default mouse speed and "Enhance Pointer Precision" are totally inompatible with this library. Starting from build 0.3 both options are reset to default values on start. You might say it is not a good idea to change user PC settings, but I think it is much better than adding a note 'reset your mouse settings' to Readme file, that is actually never read by end users.


Reading mouse coords from game memory
-----
TouchMouse from version 0.3.5 is able to read pointer coord from game memory. This gives 100% game compatibility but could not work with modified .exes and mods. Pointer addresses are stored in TouchMouse.ini file and you can alter or disable them if needed. Also there is a function to scan game memory for this address, but it is rather unstable now and disabled by default. To enable it uncomment line ;ScanKey=20 in .ini and press CapsLock (or other key if you changed the code in .ini) to start scan. This would lead to game crash in 99.9% cases but all addresses found are stored in touch.log file. Choose one, convert it from hex to decimal and put to [MemoryAddress] section.

Customizable gestures
-----
Currently .ini file allows few modifications for in-game tap gestures. It takes raw VK_ code numbers and have lot of limitations. There are plans to update the .ini to human-readable format and allow literal key names, add more gestures, etc. Note that multi-finger taps are always called sequentually, i.e. 3 finger tap is theated as regular tap + 2-finger tap + 3-finger tap at the same time.

