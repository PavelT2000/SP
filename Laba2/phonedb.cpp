#define PHONEDB_EXPORTS
#include "phonedb.h"
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

// Глобальные переменные для дескрипторов проецирования
static HANDLE g_hFile = INVALID_HANDLE_VALUE;
static HANDLE g_hMapping = NULL;
static Database* g_db = NULL;

// Проверка вхождения подстроки без учета регистра
static int ContainsCaseInsensitive(const wchar_t* text, const wchar_t* queryLower) {
    if (!text || !queryLower || queryLower[0] == L'\0') return 0;
    wchar_t textLower[256];
    wcsncpy(textLower, text, 255);
    textLower[255] = L'\0';
    CharLowerBuffW(textLower, (DWORD)wcslen(textLower));
    return wcsstr(textLower, queryLower) != NULL;
}

// 1. Открытие / создание БД методом проецирования файлов в память
PHONEDB_API int DbOpen(const wchar_t* dbFileName, const wchar_t* mappingName, int* isSecondInstance) {
    if (isSecondInstance) *isSecondInstance = 0;

    // Шаг 1: Открываем или создаем файл на диске
    // Флаги FILE_SHARE_READ | FILE_SHARE_WRITE позволяют нескольким копиям программы работать с одним файлом
    g_hFile = CreateFileW(
        dbFileName,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (g_hFile == INVALID_HANDLE_VALUE) return 0;

    // Шаг 2: Обеспечиваем размер файла под структуру Database
    DWORD size = sizeof(Database);
    DWORD currentSize = GetFileSize(g_hFile, NULL);
    if (currentSize < size) {
        SetFilePointer(g_hFile, size, NULL, FILE_BEGIN);
        SetEndOfFile(g_hFile);
    }

    // Шаг 3: Создаем именованный объект проецирования файла (File Mapping)
    // Благодаря общему имени mappingName ядро Windows даёт всем копиям доступ к одной физической памяти
    g_hMapping = CreateFileMappingW(
        g_hFile,
        NULL,
        PAGE_READWRITE,
        0,
        size,
        mappingName
    );
    if (!g_hMapping) {
        CloseHandle(g_hFile);
        return 0;
    }

    // Проверяем: если объект уже существовал, значит, это вторая или последующая копия программы
    if (GetLastError() == ERROR_ALREADY_EXISTS && isSecondInstance) {
        *isSecondInstance = 1;
    }

    // Шаг 4: Отображаем файл в адресное пространство процесса (получаем прямой указатель на ОЗУ)
    g_db = (Database*)MapViewOfFile(
        g_hMapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        size
    );
    if (!g_db) {
        CloseHandle(g_hMapping);
        CloseHandle(g_hFile);
        return 0;
    }

    return 1;
}

// 2. Закрытие базы данных и освобождение ресурсов
PHONEDB_API void DbClose(void) {
    if (g_db) {
        UnmapViewOfFile(g_db);
        g_db = NULL;
    }
    if (g_hMapping) {
        CloseHandle(g_hMapping);
        g_hMapping = NULL;
    }
    if (g_hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hFile);
        g_hFile = INVALID_HANDLE_VALUE;
    }
}

// 3. Загрузка телефонного справочника из текстового файла
PHONEDB_API int DbLoadFromText(const char* txtFileName) {
    if (!g_db) return 0;

    FILE* f = fopen(txtFileName, "rb");
    if (!f) return 0;

    char lineUtf8[1024];
    wchar_t lineW[1024];
    int loaded = 0;
    g_db->count = 0; // перезаписываем данные заново

    while (fgets(lineUtf8, sizeof(lineUtf8), f)) {
        // Пропускаем UTF-8 BOM если есть в первой строке
        const char* pLine = lineUtf8;
        if (loaded == 0 && (unsigned char)pLine[0] == 0xEF && (unsigned char)pLine[1] == 0xBB && (unsigned char)pLine[2] == 0xBF) {
            pLine += 3;
        }

        // Преобразуем строку UTF-8 в Unicode wchar_t
        if (MultiByteToWideChar(CP_UTF8, 0, pLine, -1, lineW, 1024) <= 0) {
            continue;
        }

        // Убираем \r и \n в конце строки
        lineW[wcscspn(lineW, L"\r\n")] = L'\0';
        if (lineW[0] == L'\0' || lineW[0] == L'#') continue;

        if (g_db->count >= MAX_RECORDS) break;

        Record* r = &g_db->records[g_db->count];
        memset(r, 0, sizeof(Record));

        // Разбираем строку: Телефон, Фамилия, Имя, Отчество, Улица, Дом, Корпус, Квартира
        wchar_t* token = wcstok(lineW, L",");
        int field = 0;

        while (token && field < 8) {
            // Удаляем начальные пробелы
            while (*token == L' ') token++;
            // Удаляем конечные пробелы
            int len = (int)wcslen(token);
            while (len > 0 && token[len - 1] == L' ') token[--len] = L'\0';

            switch (field) {
                case 0: wcsncpy(r->phone, token, sizeof(r->phone)/sizeof(wchar_t) - 1); break;
                case 1: wcsncpy(r->surname, token, sizeof(r->surname)/sizeof(wchar_t) - 1); break;
                case 2: wcsncpy(r->name, token, sizeof(r->name)/sizeof(wchar_t) - 1); break;
                case 3: wcsncpy(r->patronymic, token, sizeof(r->patronymic)/sizeof(wchar_t) - 1); break;
                case 4: wcsncpy(r->street, token, sizeof(r->street)/sizeof(wchar_t) - 1); break;
                case 5: wcsncpy(r->house, token, sizeof(r->house)/sizeof(wchar_t) - 1); break;
                case 6: wcsncpy(r->corpus, token, sizeof(r->corpus)/sizeof(wchar_t) - 1); break;
                case 7: wcsncpy(r->flat, token, sizeof(r->flat)/sizeof(wchar_t) - 1); break;
            }
            token = wcstok(NULL, L",");
            field++;
        }

        g_db->count++;
        loaded++;
    }

    fclose(f);
    return loaded;
}

// 4. Получение количества записей
PHONEDB_API int DbGetCount(void) {
    return g_db ? g_db->count : 0;
}

// 5. Получение одной записи
PHONEDB_API int DbGetRecord(int index, Record* outRecord) {
    if (!g_db || !outRecord || index < 0 || index >= g_db->count) return 0;
    *outRecord = g_db->records[index];
    return 1;
}

// 6. Гибкий поиск данных (по всем полям или по конкретному полю)
PHONEDB_API int DbSearch(const wchar_t* query, int fieldIndex, Record* results, int maxResults) {
    if (!g_db || !query || query[0] == L'\0') return 0;

    wchar_t queryLower[256];
    wcsncpy(queryLower, query, 255);
    queryLower[255] = L'\0';
    CharLowerBuffW(queryLower, (DWORD)wcslen(queryLower));

    int found = 0;
    for (int i = 0; i < g_db->count; ++i) {
        Record* r = &g_db->records[i];
        int match = 0;

        if (fieldIndex == -1) {
            // Умный поиск: совпадение хотя бы в одном из основных полей
            match = ContainsCaseInsensitive(r->phone, queryLower) ||
                    ContainsCaseInsensitive(r->surname, queryLower) ||
                    ContainsCaseInsensitive(r->name, queryLower) ||
                    ContainsCaseInsensitive(r->patronymic, queryLower) ||
                    ContainsCaseInsensitive(r->street, queryLower) ||
                    ContainsCaseInsensitive(r->house, queryLower);
        } else if (fieldIndex == 0) {
            match = ContainsCaseInsensitive(r->phone, queryLower);
        } else if (fieldIndex == 1) {
            match = ContainsCaseInsensitive(r->surname, queryLower);
        } else if (fieldIndex == 4) {
            match = ContainsCaseInsensitive(r->street, queryLower);
        }

        if (match) {
            if (results && found < maxResults) {
                results[found] = *r;
            }
            found++;
        }
    }

    return found;
}
