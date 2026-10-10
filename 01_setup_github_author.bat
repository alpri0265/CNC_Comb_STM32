@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

title 01 - Налаштувати автора GitHub (CNC_Comb_STM32)
cd /d "%~dp0"

REM ------------------------------------------------------------
REM  Налаштування автора комітів (перший раз запускати цей файл)
REM ------------------------------------------------------------

echo.
echo  =========================================
echo   Hala! Цей файл налаштовує автора GitHub
echo   для ПОТОЧНОГО проєкту (лише CNC_Comb_STM32).
echo  =========================================
echo.
echo  Автор, який ЗАРАЗ прописаний:
echo  -----------------------------------------
for /f "tokens=*" %%a in ('git config --local user.name')  do set OLD_NAME=%%a
for /f "tokens=*" %%a in ('git config --local user.email') do set OLD_MAIL=%%a
if "!OLD_NAME!"=="" set OLD_NAME=(нема)
if "!OLD_MAIL!"=="" set OLD_MAIL=(нема)
echo   Логін : !OLD_NAME!
echo   Email : !OLD_MAIL!
echo  -----------------------------------------
echo.
echo  1. Відкрий в браузері:
echo     https://github.com/settings/emails
echo     ТАМ скопіюй email, який позначено "Verified"
echo     (краще візьми той, що на noreply.github.com)
echo.
echo  2. Відкрий https://github.com/ - твій логін вгорі праворуч.
echo.
echo  --------------------
set /p NEW_NAME="Введи твій ЛОГІН на GitHub (напр. z-book): "
if "!NEW_NAME!"=="" goto ask_name
:ask_mail
set /p NEW_MAIL="Введи твій VERIFIED Email (напр. 123456+z-book@users.noreply.github.com): "
if "!NEW_MAIL!"=="" goto ask_mail
echo  --------------------
echo.
echo  Зберігаю:
echo    user.name  = !NEW_NAME!
echo    user.email = !NEW_MAIL!
echo.

REM ==== 1. Налаштувати config ====
git config --local user.name  "!NEW_NAME!"
git config --local user.email "!NEW_MAIL!"
echo  [OK] git config оновлено.

REM ==== 2. Перевизначити автора першого коміту ====
echo  [..] Перевизначаю автора першого (єдиного) коміту ...
git commit --amend --reset-author --no-edit >nul 2>&1
if errorlevel 1 (
    echo  [!!] Не вдалося amend-коміт (можливо, нема жодного коміту).
    goto end_err
)
echo  [OK] Автор коміту оновлено.

REM ==== 3. Перевірка ====
echo.
echo  =========================================
echo   Поточний автор останнього коміту:
for /f "tokens=*" %%a in ('git log -1 --format=%%an') do set CUR_NAME=%%a
for /f "tokens=*" %%a in ('git log -1 --format=%%ae') do set CUR_MAIL=%%a
echo    Ім'я   : !CUR_NAME!
echo    Email  : !CUR_MAIL!
echo  =========================================
echo.
echo  [ГОТОВО] Автора налаштовано.
echo.
echo  Тепер двічі клацни на файл 02_push_to_github.bat
echo  (Він попросить URL порожнього репозиторію з GitHub).
echo.
pause
exit /b 0

:end_err
echo.
echo [ПОМИЛКА] Щось пішло не так. Зроби скріншот вікна і напиши агенту.
pause
exit /b 1
