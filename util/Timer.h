#pragma once

#include <chrono>
#include <print>

class Timer {
private:
    std::chrono::time_point<std::chrono::steady_clock> m_start, m_end;
    std::chrono::duration<float> m_duration;

public:
    Timer() {
        m_start = std::chrono::steady_clock::now();
    }

    ~Timer() {
        m_end = std::chrono::steady_clock::now();
        m_duration = std::chrono::duration_cast<std::chrono::milliseconds>(m_start - m_end);
        std::println("Timer took {} ms", m_duration.count());
    }
};
