@echo off
rem MANIQUI DE DIAGNOSTICO para NS - segunda instancia que se conecta
rem al listenserver local. En ventana pequena A PROPOSITO: no es para
rem jugar, es para tener un segundo jugador humano visible sin bots.
rem
rem ORDEN: 1) lanza test_ns_evobot.bat y quedate en el readyroom
rem        2) lanza este bat, mete al maniqui en un equipo y dejalo quieto
rem        3) vuelve a tu instancia (alt+tab), unete al otro equipo y juega
cd /d "%~dp0"
xash3d.exe -game ns -windowed -width 640 -height 480 +connect 127.0.0.1
