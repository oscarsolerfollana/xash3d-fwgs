@echo off
rem Pone el motor de master del PR 2732 (build 4178, c030d823) guardando el local
rem en los .local. Vuelve atras con restaura_motor_local.bat.
cd /d "%~dp0"
for %%f in (xash ref_gl filesystem_stdio menu) do (
  if exist %%f.dll.local echo YA estaba puesto el de master ^(existe %%f.dll.local^) & goto :fin
)
for %%f in (xash ref_gl filesystem_stdio menu) do (
  copy /y %%f.dll %%f.dll.local >nul && copy /y motor_master_2732\%%f.dll %%f.dll >nul && echo %%f.dll = master || echo ERROR con %%f.dll
)
echo.
echo  OJO: con este motor hay que lanzar SIEMPRE con -vguiloader vgui_support.dll,
echo  o el juego se cae en vgui.dll nada mas arrancar.
:fin
pause
