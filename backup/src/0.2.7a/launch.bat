@echo off
title MSHost by BaDRiVeR

rem --- Запускаем Caddy в фоне ---
start "" /b "D:\Programms\MSHost\caddy\caddy.exe" run --config "D:\Programms\MSHost\caddy\Caddyfile"

rem --- Ждем, чтобы Caddy успел запуститься ---
timeout /t 3 /nobreak > nul

rem --- Ищем PID процесса caddy.exe ---
set "CADDY_PID="
for /f "tokens=2 delims=," %%a in ('tasklist /fi "imagename eq caddy.exe" /fo csv /nh') do (
    rem Берем первый найденный PID (если у тебя только один caddy.exe — ок)
    set "CADDY_PID=%%~a"
    goto got_pid
)
:got_pid

if not defined CADDY_PID (
    echo Не удалось найти процесс Caddy!
) else (
    echo Caddy PID: %CADDY_PID%
)

rem --- Запускаем mshost.exe с аргументами из файла и командной строки ---
if not exist "mshost.exe" (
    echo ERROR! mshost.exe not found!
    pause
    exit /b 1
)

setlocal enabledelayedexpansion

set "USER_ARGS=%*"
set "FILE_ARGS="

if exist "launch_args.txt" (
    for /f "usebackq delims=" %%A in ("launch_args.txt") do (
        if not "%%A"=="" (
            set "FILE_ARGS=!FILE_ARGS! %%A"
        )
    )
)

mshost.exe %USER_ARGS% %FILE_ARGS%

rem --- После завершения mshost.exe, если есть PID, убиваем Caddy ---
if defined CADDY_PID (
    echo Останавливаю Caddy...
    taskkill /PID %CADDY_PID% /F >nul 2>&1
)

echo Программа завершена успешно!
