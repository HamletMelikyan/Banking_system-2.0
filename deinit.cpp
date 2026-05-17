#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>

const char* MEMORY_NAME = "/transp_bank";

struct Account {
    long long balance;
    long long min_balance;
    long long max_balance;
    bool frozen;
};

struct BankData {
    pthread_mutex_t mutex;
    int count;
    Account list[1];
};

int main() {
 
    int shm_fd = shm_open(MEMORY_NAME, O_RDWR, 0);
    if (shm_fd < 0) {
        perror("Ошибка открытия shared memory");
        return 1;
    }

    struct stat info;
    if (fstat(shm_fd, &info) == -1) {
        perror("Ошибка fstat");
        close(shm_fd);
        return 1;
    }

    void* data = mmap(NULL, info.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("Ошибка mmap");
        close(shm_fd);
        return 1;
    }

    close(shm_fd);

    BankData* bankPtr = (BankData*)data;

    if (pthread_mutex_destroy(&bankPtr->mutex) != 0) {
        perror("Не удалось удалить mutex");
        munmap(data, info.st_size);
        return 1;
    }

    if (munmap(data, info.st_size) == -1) {
        perror("Ошибка munmap");
        return 1;
    }

    if (shm_unlink(MEMORY_NAME) == -1) {
        perror("Ошибка shm_unlink");
        return 1;
    }

    std::cout << "Деинициализация прошла успешно\n";
    return 0;
}