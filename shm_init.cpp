#include <iostream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <cstring>

const char* SHM_FILE = "/transp_bank";

struct Account {
    long long money;
    long long min_limit;
    long long max_limit;
    bool is_blocked;
};

struct BankData {
    pthread_mutex_t mutex;
    int count;
    Account list[1]; 
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Нужно указать количество счетов\n";
        return 1;
    }

    int accCount;
    try {
        accCount = std::stoi(argv[1]);
    } catch (...) {
        std::cout << "Неверный формат числа\n";
        return 1;
    }

    if (accCount <= 0) {
        std::cout << "Количество должно быть больше нуля\n";
        return 1;
    }

    size_t totalSize = sizeof(BankData) + (accCount - 1) * sizeof(Account);

    int fd = shm_open(SHM_FILE, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        perror("Ошибка при shm_open");
        return 1;
    }

    if (ftruncate(fd, totalSize) == -1) {
        perror("Ошибка при ftruncate");
        close(fd);
        shm_unlink(SHM_FILE);
        return 1;
    }

    void* mem = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        perror("Ошибка mmap");
        close(fd);
        shm_unlink(SHM_FILE);
        return 1;
    }

    close(fd);

    BankData* bank = (BankData*)mem;
    bank->count = accCount;

    pthread_mutexattr_t mAttr;
    pthread_mutexattr_init(&mAttr);
    pthread_mutexattr_setpshared(&mAttr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&bank->mutex, &mAttr);
    pthread_mutexattr_destroy(&mAttr);

    for (int i = 0; i < accCount; i++) {
        bank->list[i].money = 0;
        bank->list[i].min_limit = 0;
        bank->list[i].max_limit = 1000000;
        bank->list[i].is_blocked = false;
    }

    std::cout << "Инициализация прошла успешно\n";
    std::cout << "Счетов создано: " << accCount << std::endl;

    return 0;
}