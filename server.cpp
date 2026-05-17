/*
 * server.cpp — TCP-сервер банка
 *
 * Запуск: ./server <port>
 *
 * Потоки:
 *   - main thread: accept() клиентов, создаёт client_thread на каждого
 *   - client_thread: читает команды от клиента, пишет ответы
 *   - stats_thread: выводит суммарное число запросов каждые 5 запросов
 *
 * Shared memory /transp_bank создаётся shm_init (из задания 1).
 * Сервер только подключается к уже существующей памяти.
 */

#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>

#include <pthread.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

// ──────────────────────────────────────────────
// Shared memory layout (must match shm_init.cpp)
// ──────────────────────────────────────────────
const char* SHARED_NAME = "/transp_bank";

struct Account {
    long long money;
    long long min_limit;
    long long max_limit;
    bool      is_blocked;
};

struct BankData {
    pthread_mutex_t mutex;
    int             count;
    Account         list[1];   // flexible array trick
};

// ──────────────────────────────────────────────
// Global state
// ──────────────────────────────────────────────
BankData*       g_bank     = nullptr;
size_t          g_shm_size = 0;

// stats
pthread_mutex_t g_stats_mtx  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_stats_cond = PTHREAD_COND_INITIALIZER;
long long       g_req_count  = 0;   // total processed requests
long long       g_last_print = 0;   // last printed threshold

volatile bool   g_shutdown   = false;
int             g_server_fd  = -1;  // server socket (для закрытия при shutdown)

// ──────────────────────────────────────────────
// Stats thread: выводит число запросов каждые 5
// ──────────────────────────────────────────────
void* stats_thread(void*) {
    pthread_mutex_lock(&g_stats_mtx);
    while (!g_shutdown) {
        pthread_cond_wait(&g_stats_cond, &g_stats_mtx);
        if (g_req_count - g_last_print >= 5) {
            g_last_print = (g_req_count / 5) * 5;
            std::cout << "[stats] Всего запросов обработано: " << g_req_count << "\n";
            std::cout.flush();
        }
    }
    pthread_mutex_unlock(&g_stats_mtx);
    return nullptr;
}

// ──────────────────────────────────────────────
// Increment request counter and signal stats
// ──────────────────────────────────────────────
void count_request() {
    pthread_mutex_lock(&g_stats_mtx);
    ++g_req_count;
    pthread_cond_signal(&g_stats_cond);
    pthread_mutex_unlock(&g_stats_mtx);
}

// ──────────────────────────────────────────────
// Send a response line to client
// ──────────────────────────────────────────────
static void send_line(int fd, const std::string& msg) {
    std::string out = msg + "\n";
    send(fd, out.c_str(), out.size(), 0);
}

// ──────────────────────────────────────────────
// Command processing (same logic as client.cpp)
// ──────────────────────────────────────────────
static bool check_id(int id) {
    return id >= 0 && id < g_bank->count;
}

static void process_command(int client_fd, const std::string& input) {
    std::istringstream ss(input);
    std::string cmd;
    ss >> cmd;

    if (cmd == "shutdown") {
        send_line(client_fd, "Сервер завершает работу...");
        g_shutdown = true;
        // Wake stats thread so it can exit
        pthread_mutex_lock(&g_stats_mtx);
        pthread_cond_signal(&g_stats_cond);
        pthread_mutex_unlock(&g_stats_mtx);
        if (g_server_fd >= 0) {
            shutdown(g_server_fd, SHUT_RDWR);
            close(g_server_fd);
            g_server_fd = -1;
        }
        return;
    }

    if (cmd == "get") {
        std::string field;
        int id;
        if (!(ss >> field >> id)) { send_line(client_fd, "Неверный ввод"); return; }
        if (!check_id(id))        { send_line(client_fd, "Нет такого счёта"); return; }

        pthread_mutex_lock(&g_bank->mutex);
        Account& a = g_bank->list[id];
        if      (field == "balance") send_line(client_fd, std::to_string(a.money));
        else if (field == "min")     send_line(client_fd, std::to_string(a.min_limit));
        else if (field == "max")     send_line(client_fd, std::to_string(a.max_limit));
        else if (field == "frozen")  send_line(client_fd, a.is_blocked ? "yes" : "no");
        else                         send_line(client_fd, "Неизвестный параметр");
        pthread_mutex_unlock(&g_bank->mutex);

    } else if (cmd == "freeze" || cmd == "unfreeze") {
        int id;
        if (!(ss >> id))   { send_line(client_fd, "Ошибка ввода"); return; }
        if (!check_id(id)) { send_line(client_fd, "Нет такого счёта"); return; }

        pthread_mutex_lock(&g_bank->mutex);
        g_bank->list[id].is_blocked = (cmd == "freeze");
        pthread_mutex_unlock(&g_bank->mutex);
        send_line(client_fd, "OK");

    } else if (cmd == "transfer") {
        int a, b;
        long long sum;
        if (!(ss >> a >> b >> sum)) { send_line(client_fd, "Ошибка ввода"); return; }
        if (!check_id(a) || !check_id(b) || sum <= 0 || a == b) {
            send_line(client_fd, "Некорректные данные"); return;
        }

        pthread_mutex_lock(&g_bank->mutex);
        Account& A = g_bank->list[a];
        Account& B = g_bank->list[b];
        if (!A.is_blocked && !B.is_blocked &&
            A.money - sum >= A.min_limit &&
            B.money + sum <= B.max_limit) {
            A.money -= sum;
            B.money += sum;
            send_line(client_fd, "OK");
        } else {
            send_line(client_fd, "Операция невозможна");
        }
        pthread_mutex_unlock(&g_bank->mutex);

    } else if (cmd == "add_all" || cmd == "sub_all") {
        long long x;
        if (!(ss >> x) || x <= 0) { send_line(client_fd, "Ошибка"); return; }

        pthread_mutex_lock(&g_bank->mutex);
        bool good = true;
        for (int i = 0; i < g_bank->count; i++) {
            Account& a = g_bank->list[i];
            if (a.is_blocked)                             { good = false; break; }
            if (cmd == "add_all" && a.money + x > a.max_limit) good = false;
            if (cmd == "sub_all" && a.money - x < a.min_limit) good = false;
        }
        if (good) {
            for (int i = 0; i < g_bank->count; i++) {
                if (cmd == "add_all") g_bank->list[i].money += x;
                else                  g_bank->list[i].money -= x;
            }
            send_line(client_fd, "OK");
        } else {
            send_line(client_fd, "Ошибка операции");
        }
        pthread_mutex_unlock(&g_bank->mutex);

    } else if (cmd == "set") {
        std::string t;
        int id;
        long long v;
        if (!(ss >> t >> id >> v)) { send_line(client_fd, "Ошибка"); return; }
        if (!check_id(id))         { send_line(client_fd, "Нет счёта"); return; }

        pthread_mutex_lock(&g_bank->mutex);
        Account& a = g_bank->list[id];
        if (t == "min") {
            if (v <= a.max_limit && v <= a.money) a.min_limit = v;
            else send_line(client_fd, "Некорректный min");
        } else if (t == "max") {
            if (v >= a.min_limit && v >= a.money) a.max_limit = v;
            else send_line(client_fd, "Некорректный max");
        }
        pthread_mutex_unlock(&g_bank->mutex);
        send_line(client_fd, "OK");

    } else {
        send_line(client_fd, "Неизвестная команда");
    }
}

// ──────────────────────────────────────────────
// Per-client thread
// ──────────────────────────────────────────────
struct ClientArgs {
    int fd;
    struct sockaddr_in addr;
};

void* client_thread(void* arg) {
    ClientArgs* ca = (ClientArgs*)arg;
    int fd = ca->fd;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ca->addr.sin_addr, ip, sizeof(ip));
    std::cout << "[server] Клиент подключён: " << ip
              << ":" << ntohs(ca->addr.sin_port) << "\n";
    delete ca;

    send_line(fd, "Добро пожаловать в TranspBank. Введите команду (help для справки).");

    char buf[4096];
    while (!g_shutdown) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;

        buf[n] = '\0';
        // Strip trailing \r\n
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();

        if (line.empty()) continue;
        if (line == "exit") break;
        if (line == "help") {
            send_line(fd, "Команды: get balance/min/max/frozen A | freeze/unfreeze A | transfer A B X | add_all/sub_all X | set min/max A X | shutdown | exit");
            count_request();
            continue;
        }

        process_command(fd, line);
        count_request();
    }

    std::cout << "[server] Клиент отключился: " << ip << "\n";
    close(fd);
    return nullptr;
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Запуск: " << argv[0] << " <port>\n";
        return 1;
    }
    int port = std::atoi(argv[1]);

    // ── Подключаемся к shared memory ──
    int shm_fd = shm_open(SHARED_NAME, O_RDWR, 0);
    if (shm_fd < 0) { perror("shm_open"); return 1; }

    struct stat st;
    if (fstat(shm_fd, &st) < 0) { perror("fstat"); return 1; }
    g_shm_size = st.st_size;

    void* mem = mmap(nullptr, g_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }
    close(shm_fd);

    g_bank = (BankData*)mem;
    std::cout << "[server] Shared memory подключена. Счетов: " << g_bank->count << "\n";

    // ── Запускаем stats thread ──
    pthread_t stats_tid;
    pthread_create(&stats_tid, nullptr, stats_thread, nullptr);

    // ── Создаём серверный сокет ──
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(g_server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(g_server_fd, 10) < 0)                           { perror("listen"); return 1; }

    std::cout << "[server] Слушаем порт " << port << "\n";

    // ── Accept loop ──
    while (!g_shutdown) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (g_shutdown) break;
            perror("accept");
            continue;
        }

        auto* ca = new ClientArgs{client_fd, client_addr};
        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, client_thread, ca);
        pthread_attr_destroy(&attr);
    }

    // ── Cleanup ──
    std::cout << "[server] Завершение работы.\n";

    pthread_mutex_lock(&g_stats_mtx);
    g_shutdown = true;
    pthread_cond_signal(&g_stats_cond);
    pthread_mutex_unlock(&g_stats_mtx);
    pthread_join(stats_tid, nullptr);

    munmap(mem, g_shm_size);
    return 0;
}
