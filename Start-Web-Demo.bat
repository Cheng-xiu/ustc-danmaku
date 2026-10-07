@echo off
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js 22.12+ is required to run the local HTTP server.
  pause
  exit /b 1
)
set "DEMO_WEB_DIR=apps\web\dist"
if exist "%DEMO_WEB_DIR%\index.html" goto serve
set "DEMO_WEB_DIR=web\dist"
if exist "%DEMO_WEB_DIR%\index.html" goto serve
echo Build the demo first. See docs\web-demo-guide.md.
pause
exit /b 1
:serve
node scripts\serve-web.mjs "%DEMO_WEB_DIR%" 4173 --open
pause
