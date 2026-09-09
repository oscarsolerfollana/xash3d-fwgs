@echo off
cls
echo ===========================================================
echo   NS 3.0 (clasico 2005) - pistola con bind NORMAL
echo ===========================================================
echo.
echo  Version clasica anterior a la 3.3b9. Bind mouse1 = +attack
echo  normal (sin el truco semi-fire).
echo.
echo  PRUEBA:
echo   1. Unete a los marines y entra a la partida.
echo   2. Saca la PISTOLA (rueda / tecla 2). Cargador = 10.
echo   3. Dispara UN click y mira el contador:
echo        - baja a 9 (-1) -> la 3.0 dispara NORMAL
echo               => el doble es un fallo introducido en la 3.3
echo        - baja a 8 (-2) -> tambien dobla aqui
echo               => es de siempre en NS, no de la 3.3
echo   4. Dime a cuanto baja con un click.
echo.
echo  (Sin metamod ni bots: gamedll directo a ns.dll.)
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -rodir C:/Users/Oscar/Proyectos/xash3d -game ns32 -fullscreen +sv_cheats 1 +maxplayers 6 +bind q lastinv +map ns_eclipse
