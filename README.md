# MyThreadPool

基于可变参数模板实现的线程池

使用现代C++特性：

* ``std::future``、``std::packaged_task``
* ``std::forward``、万能引用、可变参数模板、``std::move``、右值引用
* ``std::shared_ptr``、``std::unique_ptr``、``std::make_shared``、``std::make_unique``
* ``unique_lock`` 、``mutex``、``condition_variable``、``atomic``原子类型
* ``auto``、``decltype``、函数返回类型后置
* lambda表达式
* enum class、``constexpr``

功能：

- 能够通过SubmitTask接口，提交任何类型和任何个数的参数，并返回一个future对象存储返回值；
- 能够选择线程池运行模式，线程数量固定的fixed模式、线程数量动态变化的cached模式
