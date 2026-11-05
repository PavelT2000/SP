# Сборка ЛР4 с Linux кросс-компилятором MinGW, запуск: wine Laba4.exe
CXX = x86_64-w64-mingw32-g++
FLAGS = -O2 -Wall -finput-charset=UTF-8

all: PhoneDb.dll Laba4.exe

PhoneDb.dll: ../Laba2/phonedb.cpp ../Laba2/phonedb.h
	$(CXX) -shared -o $@ ../Laba2/phonedb.cpp $(FLAGS) -static-libgcc -static-libstdc++ -Wl,--out-implib,libPhoneDb.a

Laba4.exe: main.cpp PhoneDb.dll
	$(CXX) main.cpp -o $@ $(FLAGS) -static -L. -lPhoneDb

clean:
	rm -f PhoneDb.dll libPhoneDb.a Laba4.exe phone_book.db report.txt

.PHONY: all clean
