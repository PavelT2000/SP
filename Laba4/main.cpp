/* =====================================================================
   Системное программирование. Лабораторная работа №4
   "Механизмы синхронизации"

   Параллельный поиск в телефонном справочнике из ЛР2 (используется та же
   библиотека PhoneDb.dll, собранная из ../Laba2/phonedb.cpp).

     queries.txt  --> [главный поток] --pipe--> [рабочий поток 1] --\
                                      --pipe--> [рабочий поток 2] ---+--pipe--> [поток-сборщик] --> report.txt
                                      --pipe--> [рабочий поток N] --/

   1. Главный поток читает запросы из queries.txt и раздаёт их рабочим
      потокам по кругу (запрос i -> поток i % N). У каждого рабочего потока
      своя труба заданий (CreatePipe).
   2. Рабочий поток читает запрос из своей трубы, ищет через DbSearch()
      и пишет результат в ОБЩУЮ трубу результатов.
   3. Поток-сборщик читает общую трубу и собирает результаты.
   4. Главный поток записывает итоговый отчёт в report.txt.

   Используемые механизмы синхронизации:
     * трубы (pipes) - ReadFile на пустой трубе блокирует поток, пока
       в неё не запишут данные; закрытие пишущего конца = сигнал "конец";
     * критическая секция - чтобы сообщения разных потоков не
       перемешались в общей трубе результатов;
     * WaitForMultipleObjects / WaitForSingleObject - ожидание потоков.

   Сборка: build.bat
   ===================================================================== */
#include "../Laba2/phonedb.h"
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

#define THREAD_COUNT 4      // число рабочих потоков
#define MAX_RESULTS  100    // максимум найденных записей на один запрос

/* Сообщение "главный поток -> рабочий поток" (фиксированный размер) */
struct TaskMsg {
    int     index;          // номер запроса
    wchar_t query[128];     // строка поиска
};

/* Заголовок сообщения "рабочий поток -> сборщик".
   После заголовка в трубе идут textBytes байт текста (UTF-8). */
struct ResultHeader {
    int index;              // номер запроса
    int workerId;           // какой поток выполнил поиск
    int found;              // сколько записей найдено
    int textBytes;          // длина текста результата
};

/* Данные одного рабочего потока */
struct Worker {
    int    id;
    HANDLE taskRead;        // из этого конца трубы поток читает задания
    HANDLE taskWrite;       // в этот конец главный поток пишет задания
    HANDLE thread;
};

/* Один результат в отчёте */
struct Result {
    int workerId;
    int found;
    std::string text;
};

static HANDLE           g_resultRead;   // общая труба результатов: чтение (сборщик)
static HANDLE           g_resultWrite;  //                          запись (все рабочие)
static CRITICAL_SECTION g_resultLock;   // защищает запись в общую трубу
static CRITICAL_SECTION g_printLock;    // защищает вывод на консоль
static std::vector<Result> g_results;   // сюда сборщик складывает результаты

/* ----------------------- вспомогательные функции ----------------------- */

/* ReadFile из трубы может вернуть меньше байт, чем просили,
   поэтому читаем в цикле, пока не наберём нужный размер.
   Вернёт false, если труба закрыта (все пишущие концы закрыты). */
static bool ReadExact(HANDLE pipe, void* buf, DWORD size) {
    char* p = (char*)buf;
    while (size > 0) {
        DWORD got = 0;
        if (!ReadFile(pipe, p, size, &got, NULL) || got == 0) return false;
        p += got;
        size -= got;
    }
    return true;
}

static bool WriteExact(HANDLE pipe, const void* buf, DWORD size) {
    const char* p = (const char*)buf;
    while (size > 0) {
        DWORD done = 0;
        if (!WriteFile(pipe, p, size, &done, NULL)) return false;
        p += done;
        size -= done;
    }
    return true;
}

/* wchar_t (UTF-16, как в ЛР2) -> UTF-8 для файла */
static std::string ToUtf8(const wchar_t* w) {
    char buf[1024];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), NULL, NULL);
    return buf;
}

/* ------------------------------ потоки ------------------------------ */

/* Рабочий поток: читает задания из СВОЕЙ трубы, ищет, пишет в ОБЩУЮ трубу */
static DWORD WINAPI WorkerThread(void* param) {
    Worker* w = (Worker*)param;
    TaskMsg task;
    Record found[MAX_RESULTS];   // у каждого потока свой массив (на своём стеке)

    // ReadExact ждёт (блокируется), пока главный поток не пришлёт задание.
    // Когда главный поток закроет трубу - цикл закончится.
    while (ReadExact(w->taskRead, &task, sizeof(task))) {
        int n = DbSearch(task.query, -1, found, MAX_RESULTS);   // поиск из DLL ЛР2

        // Формируем текст результата
        std::string text;
        for (int i = 0; i < n && i < MAX_RESULTS; ++i) {
            const Record& r = found[i];
            text += "    " + ToUtf8(r.phone) + "  " + ToUtf8(r.surname) + " " +
                    ToUtf8(r.name) + " " + ToUtf8(r.patronymic) + ", " +
                    ToUtf8(r.street) + ", д. " + ToUtf8(r.house) +
                    ", к. " + ToUtf8(r.corpus) + ", кв. " + ToUtf8(r.flat) + "\n";
        }

        ResultHeader h = { task.index, w->id, n, (int)text.size() };

        // Заголовок и текст должны лечь в трубу ПОДРЯД, иначе сообщения
        // разных потоков перемешаются. Поэтому - критическая секция.
        EnterCriticalSection(&g_resultLock);
        WriteExact(g_resultWrite, &h, sizeof(h));
        WriteExact(g_resultWrite, text.data(), (DWORD)text.size());
        LeaveCriticalSection(&g_resultLock);

        EnterCriticalSection(&g_printLock);
        printf("  [поток %d] выполнил запрос #%d, найдено: %d\n", w->id, task.index + 1, n);
        LeaveCriticalSection(&g_printLock);
    }

    CloseHandle(w->taskRead);
    return 0;
}

/* Поток-сборщик: читает общую трубу результатов, пока её не закроют */
static DWORD WINAPI CollectorThread(void*) {
    ResultHeader h;
    while (ReadExact(g_resultRead, &h, sizeof(h))) {
        std::string text(h.textBytes, '\0');
        if (h.textBytes > 0) ReadExact(g_resultRead, &text[0], h.textBytes);

        Result& r = g_results[h.index];   // каждый результат - на своё место
        r.workerId = h.workerId;
        r.found    = h.found;
        r.text     = text;
    }
    return 0;
}

/* ------------------------------- main ------------------------------- */

int main() {
    SetConsoleOutputCP(CP_UTF8);

    // 1. Открываем базу данных из ЛР2 (проецирование файла в память)
    int second = 0;
    if (!DbOpen(L"phone_book.db", L"Local\\MinskPhoneSharedMapping", &second)) {
        printf("Ошибка: не удалось открыть базу данных\n");
        return 1;
    }
    if (DbGetCount() == 0)
        DbLoadFromText("../Laba2/phones.txt");
    printf("В базе записей: %d\n", DbGetCount());

    // 2. Читаем запросы из текстового файла
    std::vector<std::string> queries;
    FILE* in = fopen("queries.txt", "rb");
    if (!in) { printf("Ошибка: нет файла queries.txt\n"); return 1; }
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') queries.push_back(line);
    }
    fclose(in);
    printf("Запросов в queries.txt: %d, рабочих потоков: %d\n\n",
           (int)queries.size(), THREAD_COUNT);
    g_results.resize(queries.size());

    InitializeCriticalSection(&g_resultLock);
    InitializeCriticalSection(&g_printLock);

    // 3. Общая труба результатов + поток-сборщик
    CreatePipe(&g_resultRead, &g_resultWrite, NULL, 0);
    HANDLE collector = CreateThread(NULL, 0, CollectorThread, NULL, 0, NULL);

    // 4. Для каждого рабочего потока - своя труба заданий и сам поток
    Worker workers[THREAD_COUNT];
    HANDLE threads[THREAD_COUNT];
    for (int i = 0; i < THREAD_COUNT; ++i) {
        workers[i].id = i + 1;
        CreatePipe(&workers[i].taskRead, &workers[i].taskWrite, NULL, 0);
        workers[i].thread = CreateThread(NULL, 0, WorkerThread, &workers[i], 0, NULL);
        threads[i] = workers[i].thread;
    }

    // 5. Раздаём запросы по трубам: запрос i -> поток (i % N)
    for (int i = 0; i < (int)queries.size(); ++i) {
        TaskMsg task = {};
        task.index = i;
        MultiByteToWideChar(CP_UTF8, 0, queries[i].c_str(), -1, task.query, 127);

        Worker& w = workers[i % THREAD_COUNT];
        EnterCriticalSection(&g_printLock);
        printf("Запрос #%d \"%s\" -> поток %d\n", i + 1, queries[i].c_str(), w.id);
        LeaveCriticalSection(&g_printLock);
        WriteExact(w.taskWrite, &task, sizeof(task));
    }

    // 6. Закрываем трубы заданий: рабочие потоки получат "конец данных" и завершатся
    for (int i = 0; i < THREAD_COUNT; ++i)
        CloseHandle(workers[i].taskWrite);
    WaitForMultipleObjects(THREAD_COUNT, threads, TRUE, INFINITE);

    // 7. Все рабочие закончили -> закрываем запись в трубу результатов,
    //    сборщик дочитает остаток и завершится
    CloseHandle(g_resultWrite);
    WaitForSingleObject(collector, INFINITE);

    // 8. Пишем отчёт
    FILE* out = fopen("report.txt", "wb");
    fprintf(out, "ОТЧЁТ: параллельный поиск в телефонном справочнике Минска\n");
    fprintf(out, "Запросов: %d, рабочих потоков: %d\n", (int)queries.size(), THREAD_COUNT);
    fprintf(out, "==========================================================\n\n");
    for (int i = 0; i < (int)queries.size(); ++i) {
        fprintf(out, "Запрос #%d: \"%s\"  (выполнил поток %d, найдено: %d)\n",
                i + 1, queries[i].c_str(), g_results[i].workerId, g_results[i].found);
        if (g_results[i].found == 0) fprintf(out, "    ничего не найдено\n");
        else                         fputs(g_results[i].text.c_str(), out);
        fprintf(out, "\n");
    }
    fclose(out);
    printf("\nГотово! Отчёт записан в report.txt\n");

    // 9. Освобождаем ресурсы
    for (int i = 0; i < THREAD_COUNT; ++i) CloseHandle(threads[i]);
    CloseHandle(collector);
    CloseHandle(g_resultRead);
    DeleteCriticalSection(&g_resultLock);
    DeleteCriticalSection(&g_printLock);
    DbClose();
    return 0;
}
