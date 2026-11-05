#include "phonedb.h"
#include <windows.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <wchar.h>

// Вывод одной записи на экран
static void PrintRecord(int index, const Record* r) {
    wprintf(L"[%3d] %-18ls | %-12ls %-10ls %-14ls | %-20ls д.%-3ls к.%-2ls кв.%-3ls\n",
            index + 1,
            r->phone,
            r->surname,
            r->name,
            r->patronymic,
            r->street,
            r->house,
            r->corpus[0] ? r->corpus : L"-",
            r->flat);
}

// Заголовок таблицы
static void PrintTableHeader(void) {
    wprintf(L"------------------------------------------------------------------------------------------------------\n");
    wprintf(L" №    Телефон            | Фамилия      Имя        Отчество       | Адрес\n");
    wprintf(L"------------------------------------------------------------------------------------------------------\n");
}

int main(void) {
    // Включаем нативный Unicode (UTF-8) режим для стандартного вывода и ввода
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stdin), _O_U8TEXT);

    const wchar_t* dbFileName = L"phone_book.db";
    const wchar_t* mappingName = L"Local\\MinskPhoneSharedMapping";

    int isSecondInstance = 0;
    if (!DbOpen(dbFileName, mappingName, &isSecondInstance)) {
        wprintf(L"Ошибка: Не удалось открыть базу данных и создать проекцию файла!\n");
        return 1;
    }

    wprintf(L"======================================================================\n");
    wprintf(L"   Телефонный справочник Минска (ЛР №2: Проецирование файлов и DLL)   \n");
    wprintf(L"======================================================================\n");
    if (isSecondInstance) {
        wprintf(L"[ИНФО] Обнаружена запущенная копия программы!\n");
        wprintf(L"[ИНФО] Данный процесс подключился к СУЩЕСТВУЮЩЕЙ общей памяти.\n");
    } else {
        wprintf(L"[ИНФО] Запущена ПЕРВАЯ копия программы.\n");
        wprintf(L"[ИНФО] Создана новая проекция файла в памяти.\n");
    }
    wprintf(L"[ИНФО] Текущее число записей в общей памяти: %d\n", DbGetCount());
    wprintf(L"======================================================================\n\n");

    int choice = -1;
    wchar_t buffer[256];

    while (choice != 0) {
        wprintf(L"\nТекущее число записей в памяти: %d\n", DbGetCount());
        wprintf(L"1. Загрузить данные из текстового файла (phones.txt)\n");
        wprintf(L"2. Показать все записи\n");
        wprintf(L"3. Гибкий поиск по всем полям (умный поиск)\n");
        wprintf(L"4. Поиск по фамилии\n");
        wprintf(L"5. Поиск по номеру телефона\n");
        wprintf(L"6. Поиск по улице\n");
        wprintf(L"0. Выход\n");
        wprintf(L"Выберите пункт: ");
        fflush(stdout);

        if (!fgetws(buffer, sizeof(buffer)/sizeof(wchar_t), stdin)) break;
        if (swscanf(buffer, L"%d", &choice) != 1) continue;

        switch (choice) {
            case 1: {
                int loaded = DbLoadFromText("phones.txt");
                wprintf(L"-> Загружено %d записей из phones.txt в общую память!\n", loaded);
                break;
            }

            case 2: {
                int total = DbGetCount();
                if (total == 0) {
                    wprintf(L"-> База данных пуста. Сначала загрузите данные (пункт 1).\n");
                    break;
                }
                wprintf(L"\nВсего записей: %d\n", total);
                PrintTableHeader();
                for (int i = 0; i < total; ++i) {
                    Record r;
                    if (DbGetRecord(i, &r)) {
                        PrintRecord(i, &r);
                    }
                }
                break;
            }

            case 3:
            case 4:
            case 5:
            case 6: {
                int fieldIndex = -1;
                const wchar_t* prompt = L"Введите строку для поиска по всем полям: ";
                if (choice == 4) { fieldIndex = 1; prompt = L"Введите фамилию для поиска: "; }
                if (choice == 5) { fieldIndex = 0; prompt = L"Введите телефон для поиска: "; }
                if (choice == 6) { fieldIndex = 4; prompt = L"Введите улицу для поиска: "; }

                wprintf(L"%ls", prompt);
                fflush(stdout);
                if (!fgetws(buffer, sizeof(buffer)/sizeof(wchar_t), stdin)) break;
                buffer[wcscspn(buffer, L"\r\n")] = L'\0';

                static Record results[MAX_RECORDS];
                int found = DbSearch(buffer, fieldIndex, results, MAX_RECORDS);

                wprintf(L"\n-> Найдено записей: %d\n", found);
                if (found > 0) {
                    PrintTableHeader();
                    int toShow = found < 50 ? found : 50;
                    for (int i = 0; i < toShow; ++i) {
                        PrintRecord(i, &results[i]);
                    }
                    if (found > 50) {
                        wprintf(L"... (показано первые 50 записей из %d)\n", found);
                    }
                }
                break;
            }

            case 0:
                wprintf(L"Завершение работы программы...\n");
                break;

            default:
                wprintf(L"Неверный пункт меню, попробуйте снова.\n");
                break;
        }
    }

    DbClose();
    return 0;
}
