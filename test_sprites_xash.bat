@echo off
cls
echo ===========================================================
echo   XASH: sprites por texFormat en modo NORMAL (sprtest)
echo ===========================================================
echo.
echo  Misma escena que en Steam. Fila ARRIBA = rendermode 0,
echo  fila ABAJO = rendermode 2 (referencia). De izq. a dcha:
echo  NORMAL, ADDITIVE, INDEXALPHA, ALPHTEST, spore2, glow01.
echo  Captura con F5 para comparar con la de Steam.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
cd /d "%~dp0"
xash3d.exe -game valve -vguiloader vgui_support.dll -fullscreen -dev 1 -log sprtest_xash.log +sv_cheats 1 +map sprtest
