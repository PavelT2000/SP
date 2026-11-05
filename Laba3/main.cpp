/* =====================================================================
   Лабораторная работа №3 - демонстрация класса ThreadPool.

   Создаём пул из 3 потоков и отдаём ему 8 задач - вызовы методов
   разных объектов. По выводу видно:
     * задачи выполняют РАЗНЫЕ потоки (разные id);
     * одновременно работают не больше 3 задач (размер пула);
     * потоки переиспользуются - новых потоков не создаётся.

   Сборка Windows:  build.bat      Сборка Linux:  make
   ===================================================================== */
#include "threadpool.h"
#include <stdio.h>

static os::Mutex g_print;   // чтобы строки разных потоков не перемешивались

static void Log(const char* text, int n = -1) {
    os::Lock(&g_print);
    if (n >= 0) printf("[поток %6lu] %s %d\n", os::CurrentThreadId() % 1000000, text, n);
    else        printf("[поток %6lu] %s\n",    os::CurrentThreadId() % 1000000, text);
    fflush(stdout);
    os::Unlock(&g_print);
}

/* ---- Класс 1: что-то считает ---- */
class Calculator {
public:
    long long results[16];

    void Factorial(int n) {             // метод с параметром
        Log("начал   факториал", n);
        long long f = 1;
        for (int i = 2; i <= n; ++i) f *= i;
        os::SleepMs(500);               // имитация долгой работы
        results[n] = f;
        Log("закончил факториал", n);
    }
};

/* ---- Класс 2: просто печатает ---- */
class Greeter {
public:
    void Hello() {                      // метод без параметров
        Log("Greeter::Hello() - метод без параметров");
        os::SleepMs(300);
    }
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);        // корректная кириллица в консоли Windows
#endif
    os::MutexInit(&g_print);

    printf("Главный поток: id %lu\n", os::CurrentThreadId() % 1000000);
    printf("Создаём пул из 3 потоков и ставим 8 задач...\n\n");

    Calculator calc;
    Greeter    greeter;
    {
        ThreadPool pool(3);

        for (int n = 5; n <= 10; ++n)
            pool.Run(&calc, &Calculator::Factorial, n);   // 6 задач с параметром
        pool.Run(&greeter, &Greeter::Hello);              // 2 задачи без параметра
        pool.Run(&greeter, &Greeter::Hello);

        pool.Wait();                    // ждём выполнения всех задач
        printf("\nВсе задачи выполнены. Результаты:\n");
        for (int n = 5; n <= 10; ++n)
            printf("  %2d! = %lld\n", n, calc.results[n]);
    }   // здесь вызывается деструктор ~ThreadPool: потоки корректно завершаются

    printf("\nПул уничтожен, все потоки завершены.\n");
    os::MutexDestroy(&g_print);
    return 0;
}
