@echo off
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js 22.12+ is required to run the local HTTP server.
  pause
  exit /b 1
)
if not exist "web\dist\index.html" (
  echo Build the demo first. See docs\web-demo-guide.md.
  pause
  exit /b 1
)
node scripts\serve-web.mjs web/dist 4173 --open
pause
