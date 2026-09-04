@echo off
cls
cd /d "%~dp0"
set "DLLWON=C:\Sierra\Half-Life\valve\dlls\hl.dll"
echo ===========================================================
echo   Xash + hl.dll de WON - ascensor c1a0b
echo ===========================================================
echo.
echo   Se cambia valve\dlls\hl.dll por la de WON, se juega, y al
echo   salir se restaura sola la tuya. NO cierres esta ventana.
echo.
echo   Origen:  %DLLWON%
echo.
echo   Haz lo mismo que antes: bloquea el ascensor NADA MAS
echo   arrancar la bajada (en el primer palmo del recorrido).
echo.
echo  Pulsa una tecla.
pause >nul
copy /y "valve\dlls\hl.dll" "valve\dlls\hl.dll.orig" >nul
copy /y "%DLLWON%" "valve\dlls\hl.dll" >nul
xash3d.exe -game valve -console -fullscreen -log ascensor_won.log -dev 2 +sv_cheats 1 +sv_trace_pushers func_platrot +map c1a0b
copy /y "valve\dlls\hl.dll.orig" "valve\dlls\hl.dll" >nul
del "valve\dlls\hl.dll.orig" >nul
echo.
echo   DLL restaurada. Log en ascensor_won.log
pause
