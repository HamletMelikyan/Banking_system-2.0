#pragma once
#include <iostream>
#include <string>
#include <vector>

// ANSI color codes
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_RESET  "\033[0m"

class Painter {
public:
    Painter(std::ostream& out,
            const std::vector<std::string>& green_words,
            const std::vector<std::string>& red_words)
        : out_(out), green_(green_words), red_(red_words) {}

    void print(const std::string& line) {
        // Tokenize and colorize word by word
        std::string result;
        std::string token;
        size_t i = 0;

        while (i <= line.size()) {
            bool is_sep = (i == line.size()) || line[i] == ' ' || line[i] == '\t' || line[i] == '\n';

            if (!is_sep) {
                token += line[i];
                ++i;
                continue;
            }

            if (!token.empty()) {
                std::string color = find_color(token);
                if (!color.empty())
                    result += color + token + COLOR_RESET;
                else
                    result += token;
                token.clear();
            }

            if (i < line.size())
                result += line[i];
            ++i;
        }

        out_ << result << "\n";
    }

    // Stream-like interface
    Painter& operator<<(const std::string& line) {
        print(line);
        return *this;
    }

private:
    std::ostream& out_;
    std::vector<std::string> green_;
    std::vector<std::string> red_;

    std::string find_color(const std::string& word) {
        for (auto& g : green_)
            if (word.find(g) != std::string::npos)
                return COLOR_GREEN;
        for (auto& r : red_)
            if (word.find(r) != std::string::npos)
                return COLOR_RED;
        return "";
    }
};
