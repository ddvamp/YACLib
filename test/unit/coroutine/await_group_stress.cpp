#include <util/time.hpp>

#include <yaclib/async/run.hpp>
#include <yaclib/coroutine/await.hpp>
#include <yaclib/coroutine/await_group.hpp>
#include <yaclib/coroutine/future_traits.hpp>
#include <yaclib/coroutine/on.hpp>
#include <yaclib/executor/manual.hpp>
#include <yaclib/executor/thread_pool.hpp>

#include <array>
#include <exception>
#include <stack>
#include <utility>
#include <yaclib_std/thread>

#include <gtest/gtest.h>

namespace test {
namespace {

using namespace std::chrono_literals;
void Stress1(const std::size_t kWaiters, const std::size_t kWorkers, test::util::Duration dur) {
  auto tp = yaclib::MakeThreadPool();
  util::StopWatch sw;
  while (sw.Elapsed() < dur) {
    yaclib::AwaitGroup wg;

    yaclib_std::atomic_size_t waiters_done{0};
    yaclib_std::atomic_size_t workers_done{0};

    wg.Add(kWorkers);

    auto waiter = [&]() -> yaclib::Future<void> {
      co_await On(*tp);
      co_await wg;
      waiters_done.fetch_add(1);
    };

    std::vector<yaclib::Future<void>> waiters(kWaiters);
    for (std::size_t i = 0; i < kWaiters; ++i) {
      waiters[i] = waiter();
    }

    auto worker = [&]() -> yaclib::Future<void> {
      co_await On(*tp);
      workers_done.fetch_add(1);
      wg.Done();
    };

    std::vector<yaclib::Future<void>> workers(kWorkers);
    for (std::size_t i = 0; i < kWorkers; ++i) {
      workers[i] = worker();
    }

    Wait(workers.begin(), workers.end());
    Wait(waiters.begin(), waiters.end());
    EXPECT_EQ(waiters_done.load(), kWaiters);
    EXPECT_EQ(workers_done.load(), kWorkers);
  }
  tp->HardStop();
  tp->Wait();
}

TEST(AwaitGroup, Stress1) {
#if defined(YACLIB_UBSAN) && (defined(__GLIBCPP__) || defined(__GLIBCXX__))
  GTEST_SKIP();
#endif
#if YACLIB_FAULT == 2
  GTEST_SKIP();  // Too long
#endif
  const std::size_t COROS[] = {1, 8};
  for (const auto waiters : COROS) {
    for (const auto workers : COROS) {
      Stress1(waiters, workers, 500ms);
    }
  }
}
class Goer {
 public:
  explicit Goer(yaclib::IExecutor& scheduler, yaclib::AwaitGroup& wg) : scheduler_(scheduler), wg_(wg) {
  }

  void Start(size_t steps) {
    steps_left_ = steps;
    Step();
  }

  size_t Steps() const {
    return steps_made_;
  }

 private:
  yaclib::Future<void> NextStep() {
    co_await On(scheduler_);
    Step();
    wg_.Done();
    co_return;
  }

  void Step() {
    if (steps_left_ == 0) {
      return;
    }

    ++steps_made_;
    --steps_left_;

    wg_.Add(1);
    NextStep();
  }

 private:
  yaclib::IExecutor& scheduler_;
  yaclib::AwaitGroup& wg_;
  std::size_t steps_left_;
  std::size_t steps_made_ = 0;
};

void Stress2(util::Duration duration) {
  auto scheduler = yaclib::MakeThreadPool(4);

  std::size_t iter = 0;

  util::StopWatch sw;
  while (sw.Elapsed() < duration) {
    ++iter;

    bool done = false;

    auto tester = [&scheduler, &done, iter]() -> yaclib::Future<void> {
      const size_t steps = 1 + iter % 3;

      auto wg = yaclib::AwaitGroup();

      Goer goer{*scheduler, wg};
      goer.Start(steps);

      co_await wg;

      EXPECT_EQ(goer.Steps(), steps);
      EXPECT_TRUE(steps > 0);

      done = true;
    };

    std::ignore = tester().Get();

    EXPECT_TRUE(done);
  }
  scheduler->HardStop();
  scheduler->Wait();
}

TEST(AwaitGroup, Stress2) {
#if defined(YACLIB_UBSAN) && (defined(__GLIBCPP__) || defined(__GLIBCXX__))
  GTEST_SKIP();
#endif
#if YACLIB_FAULT == 2
  GTEST_SKIP();  // Too long
#endif
  Stress2(1s);
}

}  // namespace
}  // namespace test
