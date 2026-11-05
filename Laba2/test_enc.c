#include <windows.h>
#include <stdio.h>

int main() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const wchar_t* wstr = L"Привет, Минск! Телефонный справочник\n";
    DWORD written;
    WriteConsoleW(hOut, wstr, wcslen(wstr), &written, NULL);
    return 0;
}
