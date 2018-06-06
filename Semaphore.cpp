#include "Semaphore.h"

void Semaphore::signal() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_count++;
    m_conditionVariable.notify_one();
}

void Semaphore::wait() {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (m_count == 0) {
        m_conditionVariable.wait(lock);
    }

    m_count--;
}