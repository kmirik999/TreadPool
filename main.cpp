#include <iostream>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <random>
#include <atomic>
#include <chrono>

using namespace std;
using namespace std::chrono_literals;


struct Task {
    int id;
    function<void()> func;
};

class ThreadPool {
public:
    explicit ThreadPool(int numThreads) : stop(false) {
        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] { workerThread(); });
        }
    }

    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (thread& worker : workers)
            worker.join();
    }

    // Add a task to the pool
    template <typename Func>
    int enqueue(Func func) {
        unique_lock<mutex> lock(queueMutex);
        tasks.emplace_back(Task{ nextTaskId++, function<void()>(func) });
        condition.notify_one();
        return nextTaskId - 1;
    }

    size_t pendingTasks() const {
        return tasks.size();
    }

private:
    void workerThread() {
        while (true) {
            function<void()> task;
            {
                unique_lock<mutex> lock(queueMutex);
                condition.wait(lock, [this]() {
                    return stop || !tasks.empty();
                });
                if (stop && tasks.empty())
                    return;
                task = tasks.front().func;
                tasks.pop_front();
            }
            task();
        }
    }

    vector<thread> workers;
    deque<Task> tasks;
    mutex queueMutex;
    condition_variable condition;
    atomic<bool> stop;
    atomic<int> nextTaskId{ 0 };
};

random_device rd;
mt19937 gen(rd());
uniform_int_distribution<> dis(5, 10);

int main() {
    ThreadPool pool(4);

    for (int i = 1; i <= 10; ++i) {
        int sleepTime = dis(gen);
        pool.enqueue([i, sleepTime]() {
            this_thread::sleep_for(chrono::seconds(sleepTime));
            cout << "Task " << i << " completed" << endl;
        });
    }

    while (pool.pendingTasks() > 0) {
        this_thread::sleep_for(100ms);
    }


    return 0;
}
