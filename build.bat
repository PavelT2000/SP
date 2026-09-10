@echo off
rem Сборка ЛР1 компилятором MinGW-w64 (gcc).
rem -mwindows  - оконное приложение (без консоли)
rem -municode  - точка входа wWinMain, UNICODE-версии функций WinAPI
gcc main.c -o Laba1.exe -O2 -Wall -mwindows -municode -finput-charset=UTF-8 -lgdi32 -lcomdlg32
if errorlevel 1 (
    echo.
    echo Сборка завершилась с ошибкой.
) else (
    echo.
    echo Готово: Laba1.exe
)
