@echo off
cls
cd /d "%~dp0"
echo ===========================================================
echo   Xash - saltar desde una escalera (func_ladder)
echo ===========================================================
echo.
echo   Mapa: crossfire (hay una escalera en la torre, y otra en
echo   el bunker). Si prefieres otro: stalkyard o boot_camp.
echo.
echo   QUE HACER:
echo     1) Subete a la escalera y sube un par de peldanos.
echo     2) Mira DE FRENTE a la escalera y pulsa SALTAR.
echo     3) Repite mirando de lado y de espaldas.
echo     4) Prueba tambien pulsando salto mientras subes.
echo.
echo   La traza [ladder:SV] / [ladder:CL] sale sola en consola.
echo   Lo que importa de cada linea: frac, norm y si pone JUMP.
echo.
echo   Log: escalera.log
echo.
echo  Pulsa una tecla.
pause >nul
xash3d.exe -game valve -console -fullscreen -log escalera.log -dev 2 +sv_cheats 1 +pm_trace_ladder 1 +map crossfire
echo.
echo   Log en escalera.log
pause
