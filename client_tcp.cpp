#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "colorprint.hpp"

static void show_menu() {
    std::cout <<
        "\n=== Команды ===\n"
        "get balance A\n"
        "get min A\n"
        "get max A\n"
        "get frozen A\n"
        "freeze A / unfreeze A\n"
        "transfer A B X\n"
        "add_all X / sub_all X\n"
        "set min A X / set max A X\n"
        "shutdown\n"
        "help / exit\n"
        "===============\n";
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Запуск: " << argv[0] << " <ip> <port>\n";
        return 1;
    }

    const char* host = argv[1];
    int         port = std::atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &server.sin_addr) <= 0) {
        std::cerr << "Неверный IP-адрес: " << host << "\n";
        return 1;
    }

    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect");
        return 1;
    }

    std::cout << "Подключено к " << host << ":" << port << "\n";

    Painter painter(
        std::cout,
        {"OK", "yes", "0","1","2","3","4","5","6","7","8","9"},  
        {"Ошибка", "невозможна", "Нет", "no", "Некорректн",
         "Неизвестн", "заблокир", "blocked"} 
    );

    char rbuf[4096];
    ssize_t n = recv(sock, rbuf, sizeof(rbuf) - 1, 0);
    if (n > 0) {
        rbuf[n] = '\0';
        painter.print(std::string(rbuf));
    }

    show_menu();

    std::string input;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) break;   
        if (input.empty()) continue;

        if (input == "help") { show_menu(); continue; }

        std::string msg = input + "\n";
        send(sock, msg.c_str(), msg.size(), 0);

        if (input == "exit") break;

        n = recv(sock, rbuf, sizeof(rbuf) - 1, 0);
        if (n <= 0) {
            std::cout << "Сервер закрыл соединение.\n";
            break;
        }
        rbuf[n] = '\0';

        std::string resp(rbuf);
        std::string line;
        for (char c : resp) {
            if (c == '\n') {
                if (!line.empty()) painter.print(line);
                line.clear();
            } else if (c != '\r') {
                line += c;
            }
        }
        if (!line.empty()) painter.print(line);

        if (input == "shutdown") break;
    }

    close(sock);
    return 0;
}
