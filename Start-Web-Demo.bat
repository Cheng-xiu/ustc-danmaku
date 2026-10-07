@echo off
cd /d "%~dp0"
where node >nul 2>nul
if errorlevel 1 (
  echo Node.js 22.12+ is required to run the local HTTP server.
  pause
  exit /b 1
)
if exist "apps\web\package.json" goto monorepo
set "DEMO_WEB_DIR=web\dist"
if exist "%DEMO_WEB_DIR%\index.html" goto serve
echo Download package missing web\dist\index.html. Extract the full ZIP or open ustc-danmaku.html.
pause
exit /b 1
:monorepo
set "DEMO_WEB_DIR=apps\web\dist"
if exist "%DEMO_WEB_DIR%\index.html" goto serve
echo Monorepo build missing apps\web\dist\index.html. Run npm run build from the repository root.
pause
exit /b 1
:serve
node scripts\serve-web.mjs "%DEMO_WEB_DIR%" 4173 --open
pause
