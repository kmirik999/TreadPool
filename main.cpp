#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <random>
#include <chrono>
#include <future>
#include <limits>

using namespace std;
using namespace chrono;

class ThreadPool {
public:
    explicit ThreadPool(int numThreads) : stop(false), totalWaitingTime(0), totalTasks(0), queueLengthSum(0) {
        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !taskQueue.empty(); });

                        if (stop && taskQueue.empty())
                            return;

                        task = move(taskQueue.front());
                        taskQueue.pop();
                    }

                    auto start = high_resolution_clock::now();
                    task();
                    auto end = high_resolution_clock::now();
                    auto duration = duration_cast<milliseconds>(end - start);

                    {
                        lock_guard<mutex> timeLock(timeMutex);
                        totalWaitingTime += duration.count();
                        ++totalTasks;
                    }
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> future<typename result_of<F(Args...)>::type> {
        using returnType = typename result_of<F(Args...)>::type;
        auto task = make_shared<packaged_task<returnType()>>([Func = forward<F>(f)] { return Func(); });
        future<returnType> result = task->get_future();
        {
            lock_guard<mutex> lock(queueMutex);
            if (stop)
                throw runtime_error("enqueue on stopped ThreadPool");

            taskQueue.emplace([task]() { (*task)(); });
            queueLengthSum += taskQueue.size();
        }

        condition.notify_one();
        return result;
    }

    void stopPool() {
        {
            std::lock_guard<std::mutex> lock(std::mutex);
            stop = true;
        }

        condition.notify_all();

        for (thread& worker : workers) {
            worker.join();
        }
    }

    double getAverageWaitingTime() const {
        lock_guard<mutex> timeLock(timeMutex);
        return static_cast<double>(totalWaitingTime) / totalTasks;
    }

    double getAverageQueueLength() const {
        std::lock_guard<std::mutex> lock(std::mutex);
        return static_cast<double>(queueLengthSum) / totalTasks;
    }

private:
    vector<thread> workers;
    queue<function<void()>> taskQueue;
    mutex queueMutex;
    condition_variable condition;
    bool stop;
    int64_t totalWaitingTime;
    int totalTasks;
    int64_t queueLengthSum;
    mutable mutex timeMutex;
};

int main() {
    int numThreads;
    int numTasks;
    int queueLimit;

    cout << "Enter the number of worker threads: ";
    cin >> numThreads;

    cout << "Enter the number of tasks to execute: ";
    cin >> numTasks;

    cout << "Enter the queue limit (0 for unlimited): ";
    cin >> queueLimit;

    ThreadPool threadPool(numThreads);

    random_device rd;
    mt19937 generator(rd());
    uniform_int_distribution<int> distribution(5, 10);

    int maxTimeToFillQueue = 0;
    int minTimeToFillQueue = numeric_limits<int>::max();
    int rejectedTasks = 0;

    auto start = high_resolution_clock::now();

    for (int i = 0; i < numTasks; ++i) {
        int sleepTime = distribution(generator);

        if (queueLimit > 0 && threadPool.getAverageQueueLength() > queueLimit) {
            rejectedTasks++;
        } else {
            if (i % 3 == 0) {

                threadPool.enqueue([i, sleepTime]() {
                    this_thread::sleep_for(seconds(sleepTime));
                    int result = 5 * 4;  // Replace with your actual multiplication operation
                    cout << "Multiplication Task " << i << " completed. Result: " << result << endl;
                });
            } else if (i % 3 == 1) {

                threadPool.enqueue([i, sleepTime]() {
                    this_thread::sleep_for(seconds(sleepTime));
                    int result = 10 - 2;  // Replace with your actual subtraction operation
                    cout << "Subtraction Task " << i << " completed. Result: " << result << endl;
                });
            } else {
                // Division task
                threadPool.enqueue([i, sleepTime]() {
                    this_thread::sleep_for(seconds(sleepTime));
                    double result = 20.0 / 5;
                    cout << "Division Task " << i << " completed. Result: " << result << endl;
                });
            }
        }

        if (threadPool.getAverageQueueLength() > 0 && minTimeToFillQueue == numeric_limits<int>::max()) {
            int timeToFillQueue = static_cast<int>(duration_cast<seconds>(high_resolution_clock::now() - start).count());
            maxTimeToFillQueue = max(maxTimeToFillQueue, timeToFillQueue);
            minTimeToFillQueue = min(minTimeToFillQueue, timeToFillQueue);
        }
    }

    this_thread::sleep_for(seconds(1));

    auto end = high_resolution_clock::now();
    auto duration = duration_cast<seconds>(end - start);
    double averageWaitingTime = threadPool.getAverageWaitingTime();
    double averageQueueLength = threadPool.getAverageQueueLength();

    cout << "Statistics:" << endl;
    cout << "Number of threads created: " << numThreads << endl;
    cout << "Average time a thread is in the waiting state: " << averageWaitingTime << " ms" << endl;
    cout << "Maximum time until the queue was filled: " << maxTimeToFillQueue << " s" << endl;
    cout << "Minimum time until the queue was filled: " << (minTimeToFillQueue == numeric_limits<int>::max() ? 0 : minTimeToFillQueue) << " s" << endl;
    cout << "Number of rejected tasks: " << rejectedTasks << endl;
    cout << "Average task execution time: " << duration.count() / static_cast<double>(numTasks) << " s" << endl;
    cout << "Average queue length: " << averageQueueLength << endl;

    threadPool.stopPool();

    return 0;
}