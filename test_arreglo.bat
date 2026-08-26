@echo off
cls
echo ===========================================================
echo   PRUEBA DEL ARREGLO  -  solo Xash3D, sin Steam
echo ===========================================================
echo.
echo  Sin volcado de log, para que no te baje los fps.
echo.
echo  NO hace falta escribir NADA en la consola.
echo  (host_limitlocal deberia dar igual ahora: dejalo en 0)
echo.
echo  Dentro del juego:
echo    1. coge el M60, ponte delante de una pared, QUIETO
echo    2. rafaga de ~30 tiros SIN bipode
echo    3. despliega el bipode (tecla V)
echo    4. rafaga de ~30 tiros CON bipode
echo.
echo  Lo unico que necesito saber: si el bipode cierra el cono
echo  como en Steam, sin tocar host_limitlocal.
echo.
echo  Si quieres comparar: host_limitlocal 1 deberia notarse
echo  IGUAL que con 0. Si sigue habiendo diferencia, avisame.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game firearms -windowed -width 800 -height 600 +maxplayers 16 +map ps_island
