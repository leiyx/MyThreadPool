#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

// ----------- 线程类 -----------
class Thread {
 public:
  using ThreadFunc = std::function<void(int)>;

  Thread(ThreadFunc func);
  ~Thread() = default;

  void Start();
  int GetId() const;

 private:
  ThreadFunc func_;
  static int generateId_;
  const int thread_id_;
};

// -------------------------------
constexpr int TASK_MAX_THRESHHOLD = 2;       // 默认最多任务数量
constexpr int THREAD_MAX_THRESHHOLD = 1024;  // 默认最多线程数量
constexpr int THREAD_MAX_IDLE_TIME = 60;     // 动态线程最长空闲时间

// 线程池支持的模式
enum class PoolMode {
  MODE_FIXED,   // 固定数量的线程
  MODE_CACHED,  // 线程数量可动态增长
};

// ----------- 线程池类 -----------
class ThreadPool {
 public:
  ThreadPool();
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void SetMode(PoolMode mode);
  // 设置任务队列中任务数量上限阈值
  void SetTaskQueMaxThreshold(int threshhold);
  // 设置线程池cached模式下线程数量阈值
  void SetThreadSizeThreshold(int threshhold);

  template <typename Func, typename... Args>
  auto SubmitTask(Func&& func, Args&&... args)
      -> std::future<decltype(func(args...))>;
  void Start(int init_thread_size = std::thread::hardware_concurrency());

 private:
  // 定义线程运行函数
  void ThreadFunc(int thread_id);

 private:
  // key:该Thread对象的thread id, value:指向Thread对象的unique_ptr
  std::unordered_map<int, std::unique_ptr<Thread>>
      threads_;                        // 存放线程对象容器
  int init_thread_size_;               // 初始的线程数量
  int thread_size_threshold_;          // 线程数量上限阈值
  std::atomic<int> cur_thread_size_;   // 当前总线程数量
  std::atomic<int> idle_thread_size_;  // 当前空闲线程数量

  using Task = std::function<void()>;
  std::queue<Task> task_que_;   // 任务队列
  std::atomic<int> task_size_;  // 任务的数量
  int task_que_threshold_;      // 任务队列中任务数量上限阈值

  std::mutex mtx_;                     // 保证任务队列的线程安全
  std::condition_variable not_full_;   // 表示任务队列不满
  std::condition_variable not_empty_;  // 表示任务队列不空
  std::condition_variable exit_cond_;  // 等到线程资源全部回收

  PoolMode pool_mode_;                 // 当前线程池的工作模式
  std::atomic<bool> is_pool_running_;  // 当前线程池的启动状态
};

// 向线程池提交任务
// 使用可变参模板编程，让SubmitTask可以接收任意任务函数和任意数量的参数
// pool.SubmitTask(sum1, 10, 20); 返回一个std::future<...>
template <typename Func, typename... Args>
auto ThreadPool::SubmitTask(Func&& func, Args&&... args)
    -> std::future<decltype(func(args...))> {
  // 打包任务，放入任务队列里面
  using RType = decltype(func(args...));
  auto task = std::make_shared<std::packaged_task<RType()>>(
      std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
  std::future<RType> result = task->get_future();

  // 获取锁
  std::unique_lock<std::mutex> lock(mtx_);
  // 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
  if (!not_full_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool {
        return task_que_.size() < task_que_threshold_;
      })) {
    // 表示not_full_等待1s，条件依然没有满足
    std::cerr << "task queue is full, submit task fail." << std::endl;
    auto task = std::make_shared<std::packaged_task<RType()>>(
        []() -> RType { return RType(); });
    (*task)();
    return task->get_future();
  }

  // 如果任务队列有空余，把任务放入任务队列中
  task_que_.emplace([task]() { (*task)(); });
  ++task_size_;

  // 因为新放了任务，任务队列肯定不空了，在not_empty_上进行通知，赶快分配线程执行任务
  not_empty_.notify_one();

  // cached模式 任务处理比较紧急 场景：小而快的任务
  // 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
  if (pool_mode_ == PoolMode::MODE_CACHED && task_size_ > idle_thread_size_ &&
      cur_thread_size_ < thread_size_threshold_) {
    std::cout << ">>> create new thread..." << std::endl;
    // 创建新的线程对象
    auto ptr = std::make_unique<Thread>(
        std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
    int thread_id = ptr->GetId();
    threads_.emplace(thread_id, std::move(ptr));
    // 启动线程
    threads_[thread_id]->Start();
    // 修改线程个数相关的变量
    cur_thread_size_++;
    idle_thread_size_++;
  }

  return result;
}

#endif
