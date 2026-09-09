@echo off
rem Lanza el hl.exe de WON (CS 1.3) BAJO gdb y vuelca cada disparo a cs_won.log.
rem Sin attach: el attach persistente congela el juego y el listenserver se
rem apaga (ver ESTADO secc. 18). gdb arranca el proceso desde cero.
rem
rem Solo tienes que ejecutar ESTE bat. gdb abre el juego (arranque lento: carga
rem simbolos de las DLLs). Dentro: give weapon_ak47 y el protocolo de rafagas
rem (de pie / agachado / corriendo / saltando, pausas de 2-3 s). Al cerrar el
rem juego, gdb termina solo y el log queda en cs_won.log.
rem NO hace falta CS13_WON.bat: este bat ya lanza el juego con sus flags.

set PATH=C:\Users\Oscar\Proyectos\xash3d-tools\w64devkit\bin;%PATH%
cd /d "C:\Sierra\Half-Life"
echo Lanzando CS 1.3 (WON) bajo gdb... el arranque tarda (carga de simbolos).
rem +sv_timeout/+cl_timeout altos: bajo gdb el handshake local se retrasa y el
rem netchan tira el connection a los pocos segundos ("Server shutting down").
gdb.exe -q -batch -x "C:\Users\Oscar\Proyectos\xash3d\trace_cs_won.gdb" --args hl.exe -game cstrike13 -console -window -width 1024 -height 768 +sv_lan 1 +sv_cheats 1 +maxplayers 2 +sv_timeout 3600 +cl_timeout 3600 +map cs_assault
echo.
echo gdb terminado. Log en C:\Users\Oscar\Proyectos\xash3d\cs_won.log
pause
