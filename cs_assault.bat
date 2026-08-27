@echo off
cls
echo ===========================================================
echo   CS 1.3 - cs_assault con el pack completo
echo ===========================================================
echo.
echo    - wallhack 0.2 (paredes translucidas, mapa entero;
echo                    sin estela ya a cualquier alpha)
echo    - sv_novis 1   (el servidor manda TODAS las entidades)
echo    - bunnyhop 1   (re-salto automatico manteniendo saltar)
echo    - cap de velocidad de CS parcheado en los dlls
echo    - chat de bots apagado (crash de podbot mitigado)
echo.
echo  Ajustes en caliente por consola:
echo    wallhack 0.1-0.95 = mas/menos transparente, 0 = off
echo    bunnyhop 0 / sv_novis 0 para jugar legal
echo.
echo  Pulsa una tecla para jugar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game cstrike -fullscreen +maxplayers 16 +sv_novis 1 +wallhack 0.2 +bunnyhop 1 +map cs_assault
