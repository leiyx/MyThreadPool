/*
 * @Author: lei
 * @Description: 测试线程池功能
 * @FilePath: /线程池现代版/test.cc
 */
// compile: g++ -o test test.cc threadpool.cc -std=c++14
// run: ./test
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <thread>
using namespace std;

#include "threadpool.h"

// 假装是一个耗时任务
int sum1(int a, int b) {
  this_thread::sleep_for(chrono::seconds(2));
  return a + b;
}
// 不耗时任务
int sum2(int a, int b, int c) { return a + b + c; }

// fixed模式下提交任务以及获取返回值
void Test1() {
  ThreadPool pool;
  pool.Start(4);

  future<int> fut1 = pool.SubmitTask(sum1, 1, 2);
  future<int> fut2 = pool.SubmitTask(sum2, 1, 2, 3);
  future<int> fut3 = pool.SubmitTask(
      [](int b, int e) -> int {
        int sum = 0;
        for (int i = b; i <= e; i++) sum += i;
        return sum;
      },
      1, 100);
  future<int> fut4 = pool.SubmitTask(std::bind(sum1, 1, 2));
  cout << fut1.get() << endl;
  cout << fut2.get() << endl;
  cout << fut3.get() << endl;
  cout << fut4.get() << endl;
}

// fixed模式下提交任务超时
void Test2() {
  ThreadPool pool;
  pool.SetTaskQueMaxThreshold(4);
  pool.Start(4);

  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  cin.get();
}

// 测试线程池cached模式：动态创建线程和自动销毁空闲线程
void Test3() {
  ThreadPool pool;
  pool.SetMode(PoolMode::MODE_CACHED);
  pool.SetThreadSizeThreshold(6);
  pool.SetTaskQueMaxThreshold(4);
  pool.Start(2);

  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  pool.SubmitTask(sum1, 1, 2);
  cin.get();
}
int main() {
  // Test1();
  // Test2();
  Test3();
}