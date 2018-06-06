#pragma once
#include <mutex>
#include <condition_variable>

//https://stackoverflow.com/a/19659736

class Semaphore {
public:
    Semaphore() { m_count = 0; };
    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator = (Semaphore& other) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator = (Semaphore&& other) = delete;

    void signal();
    void wait();

private:
    std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    size_t m_count;
};