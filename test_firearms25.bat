@echo off
cls
echo ===========================================================
echo   FIREARMS 2.5  -  arsenal antiguo completo
echo ===========================================================
echo.
echo  Arranca Firearms RC2.5 como mod aparte sobre Xash3D.
echo  Aqui estan la AUG, MP5K, PSG1, M11, MC51... de serie.
echo.
echo  Es la primera vez que se prueba la 2.5 sobre Xash3D:
echo  si crashea al cargar o al elegir equipo, apunta en que
echo  momento y con que mensaje.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game firearms +maxplayers 16 +map ps_island
