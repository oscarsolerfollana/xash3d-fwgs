@echo off
cls
echo ==========================================================
echo  TEST TRANSPARENCIA SPRITE (spore2.spr, TransAlpha)
echo ==========================================================
echo.
echo  Half-Life a secas. Al cargar, espera ~10 segundos sin
echo  moverte: aparecera sola la nube verde frente al spawn.
echo.
echo  VEREDICTO: se ve translucida (pared a traves) u opaca?
echo.
echo  Extra manual si quieres comparar con el modo aditivo:
echo   ent_create env_sprite model sprites/spore.spr rendermode 5 renderamt 255 spawnflags 1 scale 2
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game valve -fullscreen +sv_cheats 1 +map crossfire +exec spore_test.cfg
