CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -O2 -Wall -finput-charset=UTF-8 -static-libgcc -static-libstdc++

all: PhoneDb.dll app.exe

PhoneDb.dll: phonedb.cpp phonedb.h
	$(CXX) -shared -o $@ phonedb.cpp $(CXXFLAGS) -Wl,--out-implib,libPhoneDb.a

app.exe: main.cpp phonedb.h PhoneDb.dll
	$(CXX) main.cpp -o $@ $(CXXFLAGS) -L. -lPhoneDb

clean:
	rm -f PhoneDb.dll libPhoneDb.a app.exe phone_book.db

.PHONY: all clean
