@echo off
cls
echo ===========================================================
echo   PRUEBA ARMAS FANTASMA 2.5  -  Firearms 3.0 + sv_cheats
echo ===========================================================
echo.
echo  Entra en partida, elige equipo/clase, y en la consola
echo  prueba ESTOS COMANDOS uno a uno (apunta que pasa con cada
echo  uno: aparece arma, no pasa nada, mensaje de error, crash):
echo.
echo    give weapon_aug
echo    give weapon_mp5k
echo    give weapon_psg1
echo    give weapon_m11
echo    give weapon_mc51
echo.
echo  Y como referencia, una que SI existe en el 3.0:
echo.
echo    give weapon_uzi
echo.
echo  Si con "give" no pasa nada, prueba tambien:
echo.
echo    weapon weapon_aug
echo.
echo  IMPORTANTE: si el juego crashea con alguna, apunta CUAL
echo  fue y sigue con las demas al volver a entrar.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game firearms -windowed -width 800 -height 600 +maxplayers 16 +sv_cheats 1 +map ps_island
