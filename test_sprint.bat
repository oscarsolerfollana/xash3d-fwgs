@echo off
cls
echo ===========================================================
echo   PRUEBA DEL SPRINT  -  solo Xash3D, sin Steam
echo ===========================================================
echo.
echo  NO hace falta escribir NADA en la consola.
echo  Tu bind de siempre ya vale:  X  y  ALT
echo.
echo  QUE COMPROBAR, por orden:
echo.
echo   1) SPRINT.  Corre hacia delante y manten pulsada la X.
echo      Tiene que esprintar MIENTRAS la aguantas, y parar
echo      al soltarla. Igual que en Steam.
echo      Prueba tambien con ALT, esta bindeada a lo mismo.
echo.
echo   2) QUE NO SE QUEDE ATASCADO.  Suelta la X y sigue
echo      corriendo normal un rato. Si sigues esprintando
echo      solo, eso es un fallo: avisame.
echo.
echo   3) QUE NO HAYA ROTO NADA MAS.  Dispara, salta, agachate,
echo      recarga, cambia de arma. Todo con normalidad.
echo.
echo   4) EL BIPODE sigue arreglado. Coge el M60, rafaga sin
echo      bipode, despliega (V), otra rafaga. Debe cerrar.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game firearms -windowed -width 800 -height 600 +maxplayers 16 +map ps_island
