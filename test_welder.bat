@echo off
cls
echo ===========================================================
echo   PRUEBA: welder contra escotillas/weldables (fix solid=5)
echo ===========================================================
echo.
echo  El motor instalado lleva el arreglo nuevo: los weldables
echo  con solid=5 (los que NO se abren al soldar) vuelven a ser
echo  tangibles para las trazas, como en GoldSrc.
echo.
echo  Pasos:
echo    1. Unete a los marines (partida en solitario, sin bots)
echo    2. En consola:  give weapon_welder
echo    3. Suelda una escotilla/rejilla de ventilacion
echo       (ns_ayumi: todas sus 8 soldables son del tipo roto)
echo.
echo  Que mirar: chispas + ruido + barra de progreso al soldar,
echo  y que al completarse dispare su puerta/evento.
echo.
echo  Control opcional: en ns_machina las escotillas son del
echo  OTRO tipo (se abren al soldar); deberian funcionar tambien.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game ns -fullscreen -dev 2 -log welder_test.log +sv_cheats 1 +maxplayers 8 +map ns_ayumi
