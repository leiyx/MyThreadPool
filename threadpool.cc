#include "threadpool.h"
// ----------- 线程类 -----------
Thread::Thread(ThreadFunc func)
    : func_(std::move(func)), thread_id_(generateId_++) {}

void Thread::Start() {
  std::thread t(func_, thread_id_);
  t.detach();
}

int Thread::GetId() const { return thread_id_; }

int Thread::generateId_ = 0;
// ----------- 线程池类 -----------

ThreadPool::ThreadPool()
    : init_thread_size_(0),
      thread_size_threshold_(THREAD_MAX_THRESHHOLD),
      idle_thread_size_(0),
      cur_thread_size_(0),
      task_size_(0),
      task_que_threshold_(TASK_MAX_THRESHHOLD),
      pool_mode_(PoolMode::MODE_FIXED),
      is_pool_running_(false) {}

ThreadPool::~ThreadPool() {
  is_pool_running_ = false;

  // 等待线程池里面所有的线程返回，有两种状态：阻塞 或 正在执行任务中
  std::unique_lock<std::mutex> lock(mtx_);
  not_empty_.notify_all();
  exit_cond_.wait(lock, [&]() -> bool { return threads_.size() == 0; });
}

// 设置线程池运行模式
// 如果线程池已经Start，那么不做任何事，直接返回
void ThreadPool::SetMode(PoolMode mode) {
  if (is_pool_running_) return;
  pool_mode_ = mode;
}
// 设置task任务队列上限阈值
// 如果线程池已经Start，那么不做任何事，直接返回
void ThreadPool::SetTaskQueMaxThreshold(int threshold) {
  if (is_pool_running_) return;
  task_que_threshold_ = threshold;
}
// 设置线程池cached模式下线程数量上限阈值
// 如果线程池已经Start，那么不做任何事，直接返回
void ThreadPool::SetThreadSizeThreshold(int threshold) {
  if (is_pool_running_) return;
  if (pool_mode_ == PoolMode::MODE_CACHED) {
    thread_size_threshold_ = threshold;
  }
}

// 开启线程池
void ThreadPool::Start(int init_thread_size) {
  is_pool_running_ = true;

  // 记录初始线程个数
  init_thread_size_ = init_thread_size;
  cur_thread_size_ = init_thread_size;
  // 创建线程对象并存入线程容器
  for (int i = 0; i < init_thread_size_; i++) {
    auto ptr = std::make_unique<Thread>(
        std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
    int thread_id = ptr->GetId();
    threads_.emplace(thread_id, std::move(ptr));
  }
  // 启动所有线程
  for (int i = 0; i < init_thread_size_; i++) {
    threads_[i]->Start();  // 需要去执行一个线程函数
    idle_thread_size_++;   // 记录初始空闲线程的数量
  }
}

// 定义线程运行函数
void ThreadPool::ThreadFunc(int thread_id) {
  auto lastTime = std::chrono::high_resolution_clock().now();

  // 所有任务必须执行完成，线程池才可以回收所有线程资源
  for (;;) {
    Task task;
    {
      // 先获取锁
      std::unique_lock<std::mutex> lock(mtx_);

      std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..."
                << std::endl;

      // cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，应该把多余的线程
      // 结束回收掉（超过init_thread_size_数量的线程要进行回收）
      // 当前时间 - 上一次线程执行的时间 > 60s

      // 每一秒中返回一次   怎么区分：超时返回？还是有任务待执行返回
      // 锁 + 双重判断
      while (task_que_.size() == 0) {
        // 线程池要结束，回收线程资源
        if (!is_pool_running_) {
          threads_.erase(thread_id);  // std::this_thread::GetId()
          std::cout << "thread_id:" << std::this_thread::get_id() << " exit!"
                    << std::endl;
          exit_cond_.notify_all();
          return;  // 线程函数结束，线程结束
        }

        if (pool_mode_ == PoolMode::MODE_CACHED) {
          // 条件变量，超时返回了
          if (std::cv_status::timeout ==
              not_empty_.wait_for(lock, std::chrono::seconds(1))) {
            auto now = std::chrono::high_resolution_clock().now();
            auto dur = std::chrono::duration_cast<std::chrono::seconds>(
                now - lastTime);
            if (dur.count() >= THREAD_MAX_IDLE_TIME &&
                cur_thread_size_ > init_thread_size_) {
              // 开始回收当前线程
              // 记录线程数量的相关变量的值修改
              // 把线程对象从线程列表容器中删除   没有办法
              // ThreadFunc《=》thread对象 thread_id => thread对象 => 删除
              threads_.erase(thread_id);  // std::this_thread::GetId()
              cur_thread_size_--;
              idle_thread_size_--;

              std::cout << "thread_id:" << std::this_thread::get_id()
                        << " exit!" << std::endl;
              return;
            }
          }
        } else {
          // 等待not_empty_条件
          not_empty_.wait(lock);
        }
      }

      idle_thread_size_--;
      std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功..."
                << std::endl;
      task = task_que_.front();
      task_que_.pop();
      task_size_--;

      // 如果任务队列依然有剩余任务，继续通知其它得线程执行任务
      if (task_que_.size() > 0) {
        not_empty_.notify_all();
      }
    }  // 释放锁
    not_full_.notify_all();

    if (task != nullptr) {
      task();
    }

    idle_thread_size_++;
    lastTime =
        std::chrono::high_resolution_clock().now();  // 更新线程执行完任务的时间
  }
}
