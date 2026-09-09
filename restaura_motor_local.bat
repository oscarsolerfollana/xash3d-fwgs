@echo off
cd /d "%~dp0"
for %%f in (xash ref_gl filesystem_stdio menu) do (
  copy /y %%f.dll.local %%f.dll >nul && del %%f.dll.local && echo %%f.dll restaurado || echo ERROR restaurando %%f.dll
)
pause
