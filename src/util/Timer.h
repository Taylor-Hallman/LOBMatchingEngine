#pragma once

#include <chrono>
#include <print>

class Timer {
private:
    std::chrono::time_point<std::chrono::steady_clock> m_start, m_end;
    std::chrono::duration<float> m_duration;
    std::chrono::duration<float> m_ignore{};

public:
    Timer() {
        m_start = std::chrono::steady_clock::now();
    }

    ~Timer() {
        using namespace std::chrono;
        m_end = steady_clock::now();
        m_duration = duration_cast<milliseconds>(m_start - m_end) - m_ignore;
        std::println("Timer took {} ms", m_duration.count());
    }
};
