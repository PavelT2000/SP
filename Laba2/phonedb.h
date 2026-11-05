#ifndef PHONEDB_H
#define PHONEDB_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef PHONEDB_EXPORTS
#define PHONEDB_API __declspec(dllexport)
#else
#define PHONEDB_API __declspec(dllimport)
#endif

#define MAX_RECORDS 5000

// Структура записи телефонного справочника Минска
// Формат: Телефон, Фамилия, Имя, Отчество, Улица, Дом, Корпус, Квартира
// Используется wchar_t (родной Unicode в Windows) для идеального отображения кириллицы без искажений
typedef struct {
    wchar_t phone[32];       // Телефон (+375 17 200-11-22)
    wchar_t surname[48];     // Фамилия (Иванов)
    wchar_t name[32];        // Имя (Иван)
    wchar_t patronymic[32];  // Отчество (Иванович)
    wchar_t street[64];      // Улица (пр-т Независимости)
    wchar_t house[16];       // Дом (10)
    wchar_t corpus[16];      // Корпус (1)
    wchar_t flat[16];        // Квартира (42)
} Record;

// Структура базы данных, расположенная прямо в спроецированной памяти
typedef struct {
    int count;                   // Количество записей в базе
    Record records[MAX_RECORDS]; // Массив записей в общей памяти
} Database;

// Открытие / создание БД через проецирование файла в память
// isSecondInstance вернет 1, если эта копия программы подключилась к уже открытой проекции
PHONEDB_API int  DbOpen(const wchar_t* dbFileName, const wchar_t* mappingName, int* isSecondInstance);

// Закрытие проекции и освобождение дескрипторов
PHONEDB_API void DbClose(void);

// Загрузка телефонного справочника из текстового файла
PHONEDB_API int  DbLoadFromText(const char* txtFileName);

// Получение общего количества записей в памяти
PHONEDB_API int  DbGetCount(void);

// Получение одной записи по индексу (от 0 до count-1)
PHONEDB_API int  DbGetRecord(int index, Record* outRecord);

// Гибкий поиск:
// fieldIndex = -1 : поиск по ВСЕМ полям сразу (умный поиск)
// fieldIndex = 0  : поиск только по телефону
// fieldIndex = 1  : поиск только по фамилии
// fieldIndex = 4  : поиск только по улице
PHONEDB_API int  DbSearch(const wchar_t* query, int fieldIndex, Record* results, int maxResults);

#ifdef __cplusplus
}
#endif

#endif // PHONEDB_H
