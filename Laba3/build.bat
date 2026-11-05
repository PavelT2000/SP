@echo off
rem Сборка ЛР3 под Windows компилятором MinGW-w64 (g++)
rem -static - чтобы exe запускался без DLL от MinGW
g++ main.cpp -o Laba3.exe -O2 -Wall -static
if errorlevel 1 (
    echo.
    echo Сборка завершилась с ошибкой.
) else (
    echo.
    echo Готово: Laba3.exe
)
