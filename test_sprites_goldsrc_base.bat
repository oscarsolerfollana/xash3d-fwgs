@echo off
cls
echo ===========================================================
echo   GOLDSRC (Steam): sprites por texFormat en modo NORMAL
echo   gl_spriteblend = %~1
echo ===========================================================
echo.
echo  Al aparecer, tienes delante 6 sprites en dos filas:
echo    Fila ARRIBA  = rendermode 0 (normal)  <- lo que medimos
echo    Fila ABAJO   = rendermode 2 (textura)  <- referencia
echo  De izquierda a derecha:
echo    NORMAL, ADDITIVE, INDEXALPHA, ALPHTEST (mismo dibujo,
echo    solo cambia el texFormat), spore2 (NS), glow01 (HL)
echo.
echo  Anota de la fila de arriba: cual es opaco (cuadrado negro
echo  alrededor), cual translucido, cual aditivo (suma luz).
echo  Haz captura (F5) para comparar luego.
echo.
echo  Pulsa una tecla para empezar.
pause >nul
set "HLDIR=C:\Program Files (x86)\Steam\steamapps\common\Half-Life"
"%HLDIR%\hl.exe" -game valve -console -full +developer 1 +sv_cheats 1 +gl_spriteblend %~1 +map sprtest
