@echo off
cls
echo ===========================================================
echo   PRUEBA: welder con el motor del PR 2732 RECORTADO
echo ===========================================================
echo.
echo  OJO: xash.dll es ahora el del PR (solo el hunk del servidor,
echo  SIN bunnyhop ni el resto de lo local). Al acabar, ejecuta
echo  restaura_motor_local.bat para volver al tuyo.
echo.
echo  Pasos:
echo    1. Unete a los marines (partida en solitario, sin bots)
echo    2. En consola:  give weapon_welder
echo    3. Suelda una rejilla de ns_ayumi (todas son del tipo roto)
echo.
echo  Que mirar: chispas + ruido + barra de progreso al soldar,
echo  y que al completarse dispare su puerta/evento.
echo.
echo  Extra (ya no lo cubre el PR): si te apoyas ANDANDO contra
echo  una rejilla, puede que la atravieses. Eso es esperado ahora.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -rodir C:/Users/Oscar/Proyectos/xash3d -game ns -vguiloader vgui_support.dll -fullscreen -dev 2 -log welder_pr.log +sv_cheats 1 +maxplayers 8 +map ns_ayumi
