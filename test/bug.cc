#include <rpmalloc.h>
#include <rpnew.h>

#include <atomic>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <stack>
#include <thread>
#include <vector>

typedef std::function<void()> Functor;

struct WorkStack {
  std::atomic<bool> stop{false};
  std::mutex mutex;
  std::condition_variable cond;
  std::stack<Functor> stack;
  std::vector<std::thread> threads;

  WorkStack(uint32_t num) {
    for (uint32_t i = 0; i < num; ++i)
      threads.emplace_back([this] { work(); });
  }

  ~WorkStack() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
    }
    cond.notify_all();
    for (auto &t : threads)
      t.join();
  }

  void add(Functor f) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stack.push(f);
    }
    cond.notify_one();
  }

  void work() {
    while (true) {
      std::unique_lock<std::mutex> lock(mutex);
      cond.wait(lock, [&] { return stop || !stack.empty(); });
      if (stop)
        break;
      auto f = stack.top();
      stack.pop();
      lock.unlock();
      f();
    }
  }
};

struct Job {
  Functor f;

  void work() {
    f();
    f = {};
  }
};

struct Latch {
  std::promise<void> promise;
  std::atomic<uint32_t> count;

  Latch(uint32_t c) : count(c) {}
  ~Latch() { promise.get_future().wait(); }

  void dec() {
    if (--count == 0)
      promise.set_value();
  }
};

int main(int, char **) {
  const uint32_t numThreads = 24;
  const uint32_t batchSize = 256;
  const uint32_t numJobs = batchSize * (numThreads * 32) - 1;

  size_t global = 0;
  WorkStack st(numThreads);

  uint32_t iteration = 0;
  std::vector<Job> jobs(numJobs);
  std::function<void(uint32_t)> func = [&](uint32_t i) { jobs[i].work(); };
  do {
    uint32_t i;
    for (i = 0; i < numJobs; ++i)
      jobs[i].f = [&] { ++global; };

    const uint32_t num = numJobs / batchSize;
    Latch l(num);
    uint32_t j;
    for (i = 0, j = batchSize; j < numJobs; i = j, j += batchSize) {
      st.add([=, &l] {
        for (uint32_t k = i; k < j; ++k)
          func(k);
        l.dec();
      });
    }

    for (; i < numJobs; ++i)
      jobs[i].work();

    if (++iteration % 256 == 0)
      std::cout << iteration << '.';
  } while (true);

  return 0;
}
