@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

title 02 - Відправити на GitHub (CNC_Comb_STM32)
cd /d "%~dp0"

echo.
echo  =========================================
echo   Відправка локального проєкту на GitHub
echo   (Потрібно ПОПЕРЕДНЄ створити ПОРОЖНІЙ
echo    репозиторій на github.com).
echo  =========================================
echo.
echo  ПЕРЕД ЦИМ КРОКОМ:
echo   1. Запусти 01_setup_github_author.bat (має бути написано [OK])
echo   2. Відкрий https://github.com/new
echo      - Repository name: CNC_Comb_STM32
echo      - [_] Private (рекомендовано)
echo      - [_] Add README     - НЕ ВІДМІЧАТИ
echo      - [_] Add .gitignore - НЕ ВІДМІЧАТИ
echo      - [_] Choose License  - НЕ ВІДМІЧАТИ
echo      - натисни Create repository
echo   3. На наступній сторінці з'явиться URL.
echo      НАТИСНИ кнопку копіювати (біля Quick setup).
echo      URL виглядає як:
echo        https://github.com/ТВОЙ_ЛОГІН/CNC_Comb_STM32.git
echo          - АБО -
echo        git@github.com:ТВОЙ_ЛОГІН/CNC_Comb_STM32.git
echo.

:ask_url
echo  --------------------
set /p REPO_URL="Встав сюди URL порожнього репозиторію: "
if "!REPO_URL!"=="" goto ask_url
echo  --------------------
echo.

REM ==== Чи є вже origin? Якщо так — спочатку видалимо.
for /f "tokens=*" %%x in ('git remote') do (
    if /i "%%x"=="origin" (
        echo  [..] Видаляю старий remote origin (якщо був)...
        git remote remove origin >nul 2>&1
    )
)

echo  [..] Додаю remote origin = !REPO_URL!
git remote add origin "!REPO_URL!"
if errorlevel 1 (
    echo.
    echo [ПОМИЛКА] НЕ ВДАЛОСЯ додати remote. Можливо, URL некоректний.
    echo              Перевір, чи ти вставив саме .git URL.
    pause
    goto ask_url
)

echo  [OK] remote origin додано.
git remote -v
echo.

REM ==== push -u origin main
echo  [..] Відправляю на GitHub (перший push)...
echo       З'явиться вікно GitHub Login — ввійди через браузер.
echo.
git push -u origin main
if errorlevel 1 (
    echo.
    echo [ПОМИЛКА] push не пройшов. Чому так може бути:
    echo  1. Не ввійшов в акаунт (відмінив вікно авторизації)
    echo  2. URL вказує на ІСНУЮЧИЙ репо З ФАЙЛАМИ (потрібно було порожній)
    echo  3. Немає інтернету.
    echo.
    echo  Виправ і запусти 02_push_to_github.bat ще раз.
    pause
    exit /b 1
)

echo.
echo  ================= [ГОТОВО!] =================
echo   Проєкт залито на GitHub.
echo.
echo   Відкрий у браузері:
echo     !REPO_URL!
echo.
echo   Там мають бути папки:
echo      include/  src/
echo   і файли: .gitignore, .gitattributes, platformio.ini
echo.
echo   Наступні рази для відправки змін:
echo     Запусти 03_commit_and_push.bat  (якщо є у папці)
echo     або введи сам:
echo        git add -A
echo        git commit -m "описи змину коротко"
echo        git push
echo.
pause
exit /b 0
