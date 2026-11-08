#pragma once
// Run the real MQTT task on a host thread, with blocking FreeRTOS-style queues.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

constexpr int pdTRUE = 1, pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
struct MockTaskStopped {};
struct MockQueue {
    size_t capacity, itemSize;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<uint8_t>> items;
    MockQueue(size_t capacity, size_t size) : capacity(capacity), itemSize(size) {}
};
using QueueHandle_t = MockQueue *;
using TaskHandle_t = std::thread *;
inline std::atomic<bool> mockTasksStopped{false};
inline bool mockFailTaskCreate = false, mockFailQueueCreate = false;
inline thread_local bool mockTaskThread = false;
inline std::vector<QueueHandle_t> mockQueues;
inline std::vector<TaskHandle_t> mockTasks;

inline QueueHandle_t xQueueCreate(size_t capacity, size_t size) {
    if (mockFailQueueCreate) return nullptr;
    auto queue = new MockQueue(capacity, size);
    mockQueues.push_back(queue);
    return queue;
}
inline int xQueueSend(QueueHandle_t queue, const void *item, uint32_t timeout) {
    assert(queue);
    std::unique_lock<std::mutex> lock(queue->mutex);
    if (timeout) queue->changed.wait(lock, [&] { return mockTasksStopped || queue->items.size() < queue->capacity; });
    if (mockTasksStopped) throw MockTaskStopped{};
    if (queue->items.size() == queue->capacity) return 0;
    auto bytes = static_cast<const uint8_t *>(item);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    queue->changed.notify_all();
    return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void *item, uint32_t timeout) {
    assert(queue);
    std::unique_lock<std::mutex> lock(queue->mutex);
    if (timeout) queue->changed.wait(lock, [&] { return mockTasksStopped || !queue->items.empty(); });
    if (mockTasksStopped) throw MockTaskStopped{};
    if (queue->items.empty()) return 0;
    memcpy(item, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    queue->changed.notify_all();
    return pdTRUE;
}
inline size_t uxQueueMessagesWaiting(QueueHandle_t queue) {
    std::lock_guard<std::mutex> lock(queue->mutex);
    return queue->items.size();
}
inline int xTaskCreate(void (*entry)(void *), const char *, uint32_t, void *arg, unsigned, TaskHandle_t *handle) {
    if (mockFailTaskCreate) return 0;
    *handle = new std::thread([=] {
        mockTaskThread = true;
        try { entry(arg); } catch (const MockTaskStopped &) {}
    });
    mockTasks.push_back(*handle);
    return pdPASS;
}
inline void stopMockTasks() {
    mockTasksStopped = true;
    for (auto queue : mockQueues) {
        std::lock_guard<std::mutex> lock(queue->mutex);
        queue->changed.notify_all();
    }
    for (auto task : mockTasks) { task->join(); delete task; }
    for (auto queue : mockQueues) delete queue;
    mockTasks.clear();
    mockQueues.clear();
    mockTasksStopped = false;
    mockFailTaskCreate = mockFailQueueCreate = false;
}
