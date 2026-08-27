@echo off
cls
echo ===========================================================
echo   CS 1.3 - cs_assault con el pack completo
echo ===========================================================
echo.
echo    - wallhack 1   (paredes translucidas, mapa entero)
echo    - sv_novis 1   (el servidor manda TODAS las entidades)
echo    - bunnyhop 1   (re-salto automatico manteniendo saltar)
echo    - cap de velocidad de CS parcheado en los dlls
echo    - chat de bots apagado (crash de podbot mitigado)
echo.
echo  Para apagar en caliente: wallhack 0 / bunnyhop 0
echo  y sv_novis 0 en la consola.
echo.
echo  Pulsa una tecla para jugar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game cstrike -fullscreen +maxplayers 16 +sv_novis 1 +wallhack 1 +bunnyhop 1 +map cs_assault
