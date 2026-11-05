@echo off
rem Сборка ЛР2 компилятором MinGW-w64 (g++)

echo [1/2] Сборка библиотеки PhoneDb.dll...
g++ -shared -o PhoneDb.dll phonedb.cpp -O2 -Wall -finput-charset=UTF-8 -static-libgcc -static-libstdc++ -Wl,--out-implib,libPhoneDb.a
if errorlevel 1 goto :fail

echo [2/2] Сборка приложения app.exe...
g++ main.cpp -o app.exe -O2 -Wall -finput-charset=UTF-8 -static-libgcc -static-libstdc++ -L. -lPhoneDb
if errorlevel 1 goto :fail

echo.
echo ======================================================
echo Сборка успешно завершена!
echo   PhoneDb.dll - динамическая библиотека ядра БД
echo   app.exe     - программа для работы со справочником
echo ======================================================
goto :eof

:fail
echo.
echo Ошибка при сборке!
exit /b 1
