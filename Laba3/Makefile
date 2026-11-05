# Сборка ЛР3 (основная версия - WinAPI):
#   make        -> Laba3.exe (кросс-компилятор MinGW), запуск: wine Laba3.exe
#   make linux  -> laba3 для Linux (доп. задание, ветка pthreads)
all:
	x86_64-w64-mingw32-g++ main.cpp -o Laba3.exe -O2 -Wall -static

linux:
	g++ main.cpp -o laba3 -O2 -Wall -pthread

clean:
	rm -f laba3 Laba3.exe

.PHONY: all linux clean
