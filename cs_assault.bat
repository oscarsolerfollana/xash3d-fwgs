@echo off
cls
echo ===========================================================
echo   CS 1.3 - cs_assault con el pack completo
echo ===========================================================
echo.
echo  Pulsa la tecla ENYE en el juego para abrir el MENU DE
echo  TRUCOS (numeros para elegir):
echo    1. Wallhack on/off (activado = 0.8)
echo    2. Aimbot: apagado / pecho / cabeza
echo    0. Cerrar
echo.
echo  Siempre activos de serie:
echo    - sv_novis 1  (el servidor manda TODAS las entidades)
echo    - bunnyhop 1  (re-salto automatico manteniendo saltar)
echo    - cap de velocidad de CS parcheado en los dlls
echo    - chat de bots apagado (crash de podbot mitigado)
echo.
echo  Ajustes finos por consola: wallhack 0.1-0.95, aimbot N
echo  (cono en grados), bunnyhop 0 / sv_novis 0 para jugar legal.
echo.
echo  Pulsa una tecla para jugar.
pause >nul
cd /d "%~dp0"
xash3d.exe -rodir C:/Users/Oscar/Proyectos/xash3d -game cstrike13 -fullscreen +maxplayers 16 +sv_novis 1 +wallhack 0 +aimbot 0 +bunnyhop 1 +map cs_assault
