@echo off
cls
cd /d "%~dp0"
echo ===========================================================
echo   Xash - saltar desde una escalera (VERIFICAR EL ARREGLO)
echo ===========================================================
echo.
echo   Lo comia nuestro parche de bunnyhop: escondia el boton de
echo   salto en el aire, y una escalera parece el aire.
echo.
echo   HAY QUE COMPROBAR LAS DOS COSAS:
echo.
echo     1) ESCALERA: subete y pulsa saltar. Tiene que DESPEGARTE.
echo        Prueba de frente, de lado y de espaldas.
echo.
echo     2) BUNNYHOP: en suelo, manten pulsado saltar y sigue
echo        botando sin parar. Tiene que seguir yendo igual.
echo.
echo   Mapa: crossfire (escalera en la torre y en el bunker).
echo   bunnyhop se deja en 2 para que diga por consola lo que hace:
echo   "bhop: pass (ladder)" en la escalera, "bhop: strip" en el aire.
echo.
echo   Log: escalera.log
echo.
echo  Pulsa una tecla.
pause >nul
xash3d.exe -rodir C:/Users/Oscar/Proyectos/xash3d -game valve -console -fullscreen -log escalera.log -dev 2 +sv_cheats 1 +bunnyhop 2 +pm_trace_ladder 1 +map crossfire
echo.
echo   Log en escalera.log
pause
