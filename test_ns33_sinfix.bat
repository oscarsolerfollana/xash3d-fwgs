@echo off
cls
echo ===========================================================
echo   EXPERIMENTO: NS 3.3b9 sobre Xash SIN el fix del physent
echo ===========================================================
echo.
echo  El motor instalado AHORA MISMO lleva el bug original
echo  restaurado a proposito. Pregunta a responder:
echo  el NS 3.3 comunitario, con jugadores/bots a la vista,
echo  peta igual que el 3.2 o su codigo aguanta?
echo.
echo  Unete a un equipo (bots 4v4 automaticos), muevete mucho,
echo  y dale unos minutos, como en las pruebas del 3.2.
echo.
echo    - Si PETA:   mismo bug, mas municion para el PR #2726
echo    - Si AGUANTA: el 3.3 blindo su codigo (el bug del motor
echo                  existe igual; lo demuestra el 3.2)
echo.
echo  Al terminar avisa: hay que REINSTALAR el motor arreglado.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game ns33 -fullscreen -dev 2 -log ns33_test.log -bugcomp get_game_dir_full+gsmrf +sv_cheats 1 +maxplayers 32 +map co_faceoff
