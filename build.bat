@echo off
rem Requer PLUGIN_SDK_DIR (E:\projetos\plugin-sdk) com Plugin.lib ja compilada (Release Win32).
if "%PLUGIN_SDK_DIR%"=="" set PLUGIN_SDK_DIR=E:\projetos\plugin-sdk
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /EHsc /std:c++17 /W3 tests\test_config.cpp /Fe:out\test_config.exe /Fo:out\ || exit /b 1
out\test_config.exe || exit /b 1
set S=%PLUGIN_SDK_DIR%
cl /nologo /EHsc /std:c++latest /W3 /O2 /MT /LD /DNDEBUG /D_CRT_NON_CONFORMING_SWPRINTFS /DGTASA /DPLUGIN_SGV_10US /DRW /DGTAGAME_NAME=\"San_Andreas\" /DGTAGAME_ABBR=\"SA\" /DGTAGAME_ABBRLOW=\"sa\" /DGTAGAME_PROTAGONISTNAME=\"CJ\" /DGTAGAME_CITYNAME=\"San_Andreas\" /DTARGET_NAME=\"FaunaSA\" ^
  /I"%S%\plugin_sa" /I"%S%\plugin_sa\game_sa" /I"%S%\plugin_sa\game_sa\enums" /I"%S%\plugin_sa\game_sa\rw" /I"%S%\shared" /I"%S%\shared\game" ^
  src\main.cpp /Fe:out\FaunaSA.asi /Fo:out\ /link /LIBPATH:"%S%\output\lib" Plugin.lib user32.lib || exit /b 1
