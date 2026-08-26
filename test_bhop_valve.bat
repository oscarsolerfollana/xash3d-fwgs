@echo off
cls
echo ===========================================================
echo   PRUEBA DEL BUNNYHOP  -  Half-Life, pista de entrenamiento
echo ===========================================================
echo.
echo  Arranca con  bunnyhop 2  ya puesto (bhop + chivato en consola).
echo.
echo  QUE HACER:
echo.
echo   1) Ve a la zona de saltos (o escribe  map t0a0d  en consola
echo      para ir directo a la seccion de saltar).
echo.
echo   2) Corre, salta y MANTEN pulsado el salto. Con bunnyhop
echo      deberia re-saltar solo cada vez que tocas suelo.
echo.
echo   3) Abre la consola y mira las lineas  bhop:
echo      - "strip (airborne)" en el aire  = el filtro actua
echo      - "pass og=..." al aterrizar     = deja pasar el salto
echo      - nada de "bhop:"                = el filtro no entra
echo.
echo   4) Para comparar sin bhop:  bunnyhop 0
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game valve -windowed -width 800 -height 600 +bunnyhop 2 +map t0a0
