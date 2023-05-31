#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <random>
#include <chrono>
#include <future>

using namespace std;
using namespace chrono;

class ThreadPool {
public:
    ThreadPool(int numThreads) : stop(false) {
        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;

                    {
                        unique_lock<mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !taskQueue.empty(); });

                        if (stop && taskQueue.empty())
                            return;

                        task = move(taskQueue.front());
                        taskQueue.pop();
                    }

                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> future<typename result_of<F(Args...)>::type> {
        using returnType = typename result_of<F(Args...)>::type;
        auto task = make_shared<packaged_task<returnType()>>(bind(std::forward<F>(f), std::forward<Args>(args)...));
        future<returnType> result = task->get_future();

        {
            unique_lock<mutex> lock(queueMutex);

            // Don't enqueue new tasks if the pool has been stopped
            if (stop)
                throw runtime_error("enqueue on stopped ThreadPool");

            taskQueue.emplace([task]() { (*task)(); });
        }

        condition.notify_one();
        return result;
    }

    void stopPool() {
        {
            unique_lock<mutex> lock(queueMutex);
            stop = true;
        }

        condition.notify_all();

        for (thread& worker : workers) {
            worker.join();
        }
    }

private:
    vector<thread> workers;
    queue<function<void()>> taskQueue;
    mutex queueMutex;
    condition_variable condition;
    bool stop;
};

int main() {
    ThreadPool threadPool(4);

    random_device rd;
    mt19937 generator(rd());
    uniform_int_distribution<int> distribution(5, 10);

    // Enqueue tasks
    vector<future<int>> results;
    for (int i = 0; i < 10; ++i) {
        int sleepTime = distribution(generator);
        results.emplace_back(threadPool.enqueue([i, sleepTime]() {
            this_thread::sleep_for(seconds(sleepTime));
            return i;
        }));
    }

    // Wait for task results and print them
    for (auto& result : results) {
        cout << "Result: " << result.get() << endl;
    }

    // Stop the thread pool
    threadPool.stopPool();

    return 0;
}
