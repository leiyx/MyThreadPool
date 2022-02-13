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

// ----------- �߳��� -----------
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
constexpr int TASK_MAX_THRESHHOLD = 2;       // Ĭ�������������
constexpr int THREAD_MAX_THRESHHOLD = 1024;  // Ĭ������߳�����
constexpr int THREAD_MAX_IDLE_TIME = 60;     // ��̬�߳������ʱ��

// �̳߳�֧�ֵ�ģʽ
enum class PoolMode {
  MODE_FIXED,   // �̶��������߳�
  MODE_CACHED,  // �߳������ɶ�̬����
};

// ----------- �̳߳��� -----------
class ThreadPool {
 public:
  ThreadPool();
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void SetMode(PoolMode mode);
  // ���������������������������ֵ
  void SetTaskQueMaxThreshold(int threshhold);
  // �����̳߳�cachedģʽ���߳�������ֵ
  void SetThreadSizeThreshold(int threshhold);

  template <typename Func, typename... Args>
  auto SubmitTask(Func&& func, Args&&... args)
      -> std::future<decltype(func(args...))>;
  void Start(int init_thread_size = std::thread::hardware_concurrency());

 private:
  // �����߳����к���
  void ThreadFunc(int thread_id);

 private:
  // key:��Thread�����thread id, value:ָ��Thread�����unique_ptr
  std::unordered_map<int, std::unique_ptr<Thread>>
      threads_;                        // ����̶߳�������
  int init_thread_size_;               // ��ʼ���߳�����
  int thread_size_threshold_;          // �߳�����������ֵ
  std::atomic<int> cur_thread_size_;   // ��ǰ���߳�����
  std::atomic<int> idle_thread_size_;  // ��ǰ�����߳�����

  using Task = std::function<void()>;
  std::queue<Task> task_que_;   // �������
  std::atomic<int> task_size_;  // ���������
  int task_que_threshold_;      // �����������������������ֵ

  std::mutex mtx_;                     // ��֤������е��̰߳�ȫ
  std::condition_variable not_full_;   // ��ʾ������в���
  std::condition_variable not_empty_;  // ��ʾ������в���
  std::condition_variable exit_cond_;  // �ȵ��߳���Դȫ������

  PoolMode pool_mode_;                 // ��ǰ�̳߳صĹ���ģʽ
  std::atomic<bool> is_pool_running_;  // ��ǰ�̳߳ص�����״̬
};

// ���̳߳��ύ����
// ʹ�ÿɱ��ģ���̣���SubmitTask���Խ������������������������Ĳ���
// pool.SubmitTask(sum1, 10, 20); ����һ��std::future<...>
template <typename Func, typename... Args>
auto ThreadPool::SubmitTask(Func&& func, Args&&... args)
    -> std::future<decltype(func(args...))> {
  // ������񣬷��������������
  using RType = decltype(func(args...));
  auto task = std::make_shared<std::packaged_task<RType()>>(
      std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
  std::future<RType> result = task->get_future();

  // ��ȡ��
  std::unique_lock<std::mutex> lock(mtx_);
  // �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
  if (!not_full_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool {
        return task_que_.size() < task_que_threshold_;
      })) {
    // ��ʾnot_full_�ȴ�1s��������Ȼû������
    std::cerr << "task queue is full, submit task fail." << std::endl;
    auto task = std::make_shared<std::packaged_task<RType()>>(
        []() -> RType { return RType(); });
    (*task)();
    return task->get_future();
  }

  // �����������п��࣬������������������
  task_que_.emplace([task]() { (*task)(); });
  ++task_size_;

  // ��Ϊ�·�������������п϶������ˣ���not_empty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
  not_empty_.notify_one();

  // cachedģʽ ������ȽϽ��� ������С���������
  // ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
  if (pool_mode_ == PoolMode::MODE_CACHED && task_size_ > idle_thread_size_ &&
      cur_thread_size_ < thread_size_threshold_) {
    std::cout << ">>> create new thread..." << std::endl;
    // �����µ��̶߳���
    auto ptr = std::make_unique<Thread>(
        std::bind(&ThreadPool::ThreadFunc, this, std::placeholders::_1));
    int thread_id = ptr->GetId();
    threads_.emplace(thread_id, std::move(ptr));
    // �����߳�
    threads_[thread_id]->Start();
    // �޸��̸߳�����صı���
    cur_thread_size_++;
    idle_thread_size_++;
  }

  return result;
}

#endif
