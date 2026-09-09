@echo off
cls
echo ===========================================================
echo   NS - prueba de bind semiauto para la pistola
echo ===========================================================
echo.
echo  Cambia mouse1 a  "+attack;-attack"  (truco GoldSrc de
echo  semi-fire: un disparo por pulsacion, suelta al instante).
echo.
echo  PRUEBA:
echo   1. Unete a los marines y entra a la partida.
echo   2. PISTOLA (rueda/tecla 2): dispara clicks.
echo        - un disparo por click (baja 10->9)?  -> ARREGLA
echo   3. LMG (tecla 1): manten pulsado.
echo        - OJO: con este bind el LMG NO disparara en rafaga,
echo          solo 1 tiro por click. Ese es el peaje.
echo   4. Dime como va cada arma.
echo.
echo  Para volver al bind normal:  en consola  bind mouse1 +attack
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -rodir C:/Users/Oscar/Proyectos/xash3d -game ns -fullscreen +sv_cheats 1 +maxplayers 6 +aimbot 0 +bunnyhop 0 +cl_lw 1 +exec semifire.cfg +map ns_machina
