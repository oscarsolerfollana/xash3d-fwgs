@echo off
cls
cd /d "%~dp0"
set "HLDIR=C:\Program Files (x86)\Steam\steamapps\common\Half-Life"
echo ===========================================================
echo   GoldSrc de Steam - ascensor c1a0b
echo ===========================================================
echo.
echo   Mismo motor GoldSrc que Sierra, pero con la hl.dll moderna
echo   (la misma exacta que carga Xash). No se toca ningun archivo.
echo.
echo   Bloquea el ascensor NADA MAS arrancar la bajada, en el primer
echo   palmo del recorrido, igual que en las otras pruebas.
echo   Camara lenta: host_timescale 0.1
echo.
echo   QUE MIRAR: al tocar el techo, si baja despacio (como en Xash)
echo   o si pega el salto seco (como en Sierra).
echo.
echo  Pulsa una tecla.
pause >nul
if exist "%HLDIR%\valve\qconsole.log" del "%HLDIR%\valve\qconsole.log" >nul
"%HLDIR%\hl.exe" -game valve -console -condebug -full +developer 2 +sv_cheats 1 +map c1a0b
echo.
echo   Al salir, avisame y leo el qconsole.log de Steam.
pause
