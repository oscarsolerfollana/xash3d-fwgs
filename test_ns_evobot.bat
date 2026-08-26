@echo off
cls
echo ===========================================================
echo   NATURAL SELECTION + EVOBOT 1.21  (via Metamod)
echo ===========================================================
echo.
echo  Los bots se anaden solos (fillteams, 8v8 en ns_machina).
echo  El bot comandante marine entra si nadie coge la silla.
echo.
echo  COMPROBAR AL ARRANCAR, en la consola:
echo    - lineas de Metamod cargando (version p109)
echo    - lineas de EvoBot cargando el navmesh del mapa
echo.
echo  Si no aparecen bots en 30-60 segundos tras unirte a un
echo  equipo, escribe en consola:   meta list
echo  y apunta lo que diga (si EvoBot esta RUN o fallo).
echo.
echo  Comandos utiles:
echo    evobot removebot all      quitar todos los bots
echo    evobot botskill Easy      bajar dificultad
echo    evobot botskill Hard      subirla
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game ns -windowed -dev 2 -log ns_evobot.log -bugcomp get_game_dir_full+gsmrf +cl_autohelp 0 +cl_highdetail 0 +sv_cheats 1 +maxplayers 32 +map co_faceoff
