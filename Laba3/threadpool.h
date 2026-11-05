/* =====================================================================
   Системное программирование. Лабораторная работа №3 "Многозадачность"

   Переносимый (portable) класс ThreadPool - работает и на Windows, и на Linux.

   Идея переносимости:
     весь платформенно-зависимый код собран в маленьком пространстве
     имён os (в начале файла). Там 3 примитива:
         поток   (Thread)  : CreateThread        / pthread_create
         мьютекс (Mutex)   : CRITICAL_SECTION    / pthread_mutex_t
         условие (CondVar) : CONDITION_VARIABLE  / pthread_cond_t
     Сам класс ThreadPool пользуется ТОЛЬКО функциями os::...,
     поэтому он одинаковый для обеих систем. Выбор делает препроцессор:
     макрос _WIN32 определён только компилятором под Windows.
   ===================================================================== */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <functional>

/* ===================== 1. Слой "операционная система" ================ */

#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <unistd.h>
#endif

namespace os {

#ifdef _WIN32
    /* ---------------- Windows (WinAPI) ---------------- */
    typedef HANDLE             Thread;
    typedef CRITICAL_SECTION   Mutex;
    typedef CONDITION_VARIABLE CondVar;
    #define THREAD_FUNC DWORD WINAPI          /* сигнатура функции потока */

    inline void MutexInit(Mutex* m)    { InitializeCriticalSection(m); }
    inline void MutexDestroy(Mutex* m) { DeleteCriticalSection(m); }
    inline void Lock(Mutex* m)         { EnterCriticalSection(m); }
    inline void Unlock(Mutex* m)       { LeaveCriticalSection(m); }

    inline void CondInit(CondVar* c)         { InitializeConditionVariable(c); }
    inline void CondDestroy(CondVar*)        { /* в Windows удалять не нужно */ }
    inline void CondWait(CondVar* c, Mutex* m) { SleepConditionVariableCS(c, m, INFINITE); }
    inline void CondSignal(CondVar* c)       { WakeConditionVariable(c); }
    inline void CondBroadcast(CondVar* c)    { WakeAllConditionVariable(c); }

    inline Thread StartThread(DWORD (WINAPI *func)(void*), void* arg) {
        return CreateThread(NULL, 0, func, arg, 0, NULL);
    }
    inline void JoinThread(Thread t) {          /* дождаться завершения потока */
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    inline unsigned long CurrentThreadId() { return GetCurrentThreadId(); }
    inline void SleepMs(int ms)            { Sleep(ms); }
#else
    /* ---------------- Linux (POSIX threads) ---------------- */
    typedef pthread_t       Thread;
    typedef pthread_mutex_t Mutex;
    typedef pthread_cond_t  CondVar;
    #define THREAD_FUNC void*                 /* сигнатура функции потока */

    inline void MutexInit(Mutex* m)    { pthread_mutex_init(m, NULL); }
    inline void MutexDestroy(Mutex* m) { pthread_mutex_destroy(m); }
    inline void Lock(Mutex* m)         { pthread_mutex_lock(m); }
    inline void Unlock(Mutex* m)       { pthread_mutex_unlock(m); }

    inline void CondInit(CondVar* c)         { pthread_cond_init(c, NULL); }
    inline void CondDestroy(CondVar* c)      { pthread_cond_destroy(c); }
    inline void CondWait(CondVar* c, Mutex* m) { pthread_cond_wait(c, m); }
    inline void CondSignal(CondVar* c)       { pthread_cond_signal(c); }
    inline void CondBroadcast(CondVar* c)    { pthread_cond_broadcast(c); }

    inline Thread StartThread(void* (*func)(void*), void* arg) {
        pthread_t t;
        pthread_create(&t, NULL, func, arg);
        return t;
    }
    inline void JoinThread(Thread t) { pthread_join(t, NULL); }
    inline unsigned long CurrentThreadId() { return (unsigned long)pthread_self(); }
    inline void SleepMs(int ms)            { usleep(ms * 1000); }
#endif

} // namespace os


/* ===================== 2. Класс ThreadPool =========================== */
/*
   Как работает пул:
     - в конструкторе создаётся N потоков; каждый сразу засыпает
       на условной переменной hasTask (состояние ОЖИДАНИЯ, CPU не тратит);
     - Run(obj, &Класс::Метод) кладёт задачу в очередь и будит
       ОДИН ждущий поток (CondSignal);
     - проснувшийся поток достаёт задачу, выполняет obj->Метод()
       и снова засыпает до следующей задачи;
     - Wait() блокирует вызывающего, пока все задачи не выполнены;
     - деструктор ставит флаг stopping, будит ВСЕ потоки (CondBroadcast)
       и дожидается их завершения (JoinThread).

   Очередь общая для всех потоков, поэтому любой доступ к ней
   выполняется только под мьютексом mutex.
*/
class ThreadPool {
public:
    explicit ThreadPool(int threadCount) : busy(0), stopping(false) {
        os::MutexInit(&mutex);
        os::CondInit(&hasTask);
        os::CondInit(&allDone);
        for (int i = 0; i < threadCount; ++i)
            threads.push_back(os::StartThread(WorkerEntry, this));
    }

    ~ThreadPool() {
        os::Lock(&mutex);
        stopping = true;               // просим потоки завершиться
        os::Unlock(&mutex);
        os::CondBroadcast(&hasTask);   // будим всех спящих

        for (size_t i = 0; i < threads.size(); ++i)
            os::JoinThread(threads[i]);

        os::CondDestroy(&allDone);
        os::CondDestroy(&hasTask);
        os::MutexDestroy(&mutex);
    }

    /* Выполнить в пуле метод без параметров:  pool.Run(&obj, &Класс::Метод); */
    template <class T>
    void Run(T* obj, void (T::*method)()) {
        Add([obj, method]() { (obj->*method)(); });
    }

    /* Выполнить в пуле метод с одним параметром: pool.Run(&obj, &Класс::Метод, 5); */
    template <class T, class A>
    void Run(T* obj, void (T::*method)(A), A arg) {
        Add([obj, method, arg]() { (obj->*method)(arg); });
    }

    /* Дождаться, пока очередь опустеет и все потоки закончат работу */
    void Wait() {
        os::Lock(&mutex);
        while (!tasks.empty() || busy > 0)
            os::CondWait(&allDone, &mutex);
        os::Unlock(&mutex);
    }

private:
    typedef std::function<void()> Task;   // "упакованный" вызов obj->method(arg)

    void Add(const Task& task) {
        os::Lock(&mutex);
        tasks.push(task);
        os::Unlock(&mutex);
        os::CondSignal(&hasTask);         // разбудить один ждущий поток
    }

    /* Точка входа потока: ОС умеет звать только обычную функцию,
       поэтому передаём this и переходим в метод WorkerLoop. */
    static THREAD_FUNC WorkerEntry(void* self) {
        ((ThreadPool*)self)->WorkerLoop();
        return 0;
    }

    void WorkerLoop() {
        for (;;) {
            os::Lock(&mutex);
            while (tasks.empty() && !stopping)
                os::CondWait(&hasTask, &mutex);   // спим, пока нет работы

            if (tasks.empty() && stopping) {      // работы нет и пора выходить
                os::Unlock(&mutex);
                return;
            }

            Task task = tasks.front();            // забираем задачу из очереди
            tasks.pop();
            busy++;
            os::Unlock(&mutex);

            task();                               // выполняем БЕЗ блокировки

            os::Lock(&mutex);
            busy--;
            if (tasks.empty() && busy == 0)
                os::CondBroadcast(&allDone);      // сообщаем Wait(): всё готово
            os::Unlock(&mutex);
        }
    }

    std::vector<os::Thread> threads;  // потоки пула
    std::queue<Task>        tasks;    // очередь задач
    os::Mutex   mutex;                // защищает tasks, busy, stopping
    os::CondVar hasTask;              // "в очереди появилась задача"
    os::CondVar allDone;              // "все задачи выполнены"
    int  busy;                        // сколько потоков сейчас выполняют задачу
    bool stopping;                    // флаг завершения пула
};

#endif // THREADPOOL_H
