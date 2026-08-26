@echo off
cls
echo ===========================================================
echo   PRUEBA A  -  servidor de STEAM (GoldSrc)
echo ===========================================================
echo.
echo  Se van a abrir DOS ventanas:
echo    1) Half-Life de Steam  = SOLO EL SERVIDOR. Ahi NO juegas.
echo    2) Xash3D              = AQUI JUEGAS Y DISPARAS.
echo.
echo  Pulsa una tecla para abrir el servidor de Steam.
pause >nul
cd /d "C:\Program Files (x86)\Steam\steamapps\common\Half-Life"
start "" hl.exe -game firearms -windowed -width 800 -height 600 +maxplayers 16 +map ps_island
cd /d "%~dp0"
cls
echo ===========================================================
echo   PRUEBA A  -  paso 2
echo ===========================================================
echo.
echo  1. Espera a que cargue el mapa en la ventana de Half-Life.
echo  2. En ESA ventana abre la consola con la tecla  ^^  y escribe:
echo.
echo         kickbot all
echo.
echo     (sin esto el servidor esta lleno de bots y no se puede entrar)
echo  3. Vuelve aqui y pulsa una tecla.
echo.
pause >nul
cls
echo  Abriendo el cliente Xash3D. En ESA ventana:
echo    - elige equipo
echo    - coge una ametralladora (M60 / M249 / PKM)
echo    - ponte delante de una pared, QUIETO
echo    - RAFAGA 1: aguanta el gatillo ~30 tiros SIN bipode
echo    - suelta, espera 2-3 segundos
echo    - despliega el bipode (tecla V)
echo    - RAFAGA 2: otros ~30 tiros CON bipode
echo    - no recargues, el M60 lleva 100 balas
echo    - sal con la consola:  quit
echo.
xash3d.exe -game firearms -dev 2 -log -windowed -width 800 -height 600 +dbg_weapondata 2 +cl_trace_events 1 +connect 127.0.0.1:27015
copy /y engine.log log_goldsrc.log >nul
cls
echo  LISTO. Guardado en  log_goldsrc.log
echo  Cierra ya la ventana de Half-Life de Steam.
echo  Ahora ejecuta  test_b_todo_xash.bat
pause
