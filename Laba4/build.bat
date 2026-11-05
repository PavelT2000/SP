@echo off
rem Сборка ЛР4 компилятором MinGW-w64 (g++)
rem Библиотека PhoneDb.dll собирается из исходников ЛР2 (..\Laba2\phonedb.cpp)

echo [1/2] Сборка PhoneDb.dll (ядро БД из ЛР2)...
g++ -shared -o PhoneDb.dll ..\Laba2\phonedb.cpp -O2 -Wall -finput-charset=UTF-8 -static-libgcc -static-libstdc++ -Wl,--out-implib,libPhoneDb.a
if errorlevel 1 goto :fail

echo [2/2] Сборка Laba4.exe...
g++ main.cpp -o Laba4.exe -O2 -Wall -finput-charset=UTF-8 -static -L. -lPhoneDb
if errorlevel 1 goto :fail

echo.
echo Готово: Laba4.exe  (запуск: Laba4.exe, результат в report.txt)
goto :eof

:fail
echo.
echo Ошибка при сборке!
exit /b 1
