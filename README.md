# mythreadpool —— C++11/17 手写线程池

基于 C++ 实现的高性能线程池，包含**两个版本**：

| 版本 | 目录 | 风格 | 一句话概括 |
|---|---|---|---|
| 版本一：手写版 | [`myHandWriteVersion/`](myHandWriteVersion/) | C++11，手写基础设施 | 从零实现 `Any`、`Result`（任务返回值）、`Semaphore`（信号量），任务以抽象基类 `Task` 表达 |
| 版本二：Modern C++ 版 | [`modernCppVersion/`](modernCppVersion/) | C++17，header-only | 用可变参模板 + 完美转发 + `std::packaged_task`/`std::future` 改造，任意可调用对象直接提交 |

版本一代码中对核心类都进行了自实现，方便学习者了解modern cpp引入的特性的底层原理，从而更好掌握。

两个版本都支持 **fixed（固定线程数）/ cached（动态扩容）** 两种模式，并且都通过了对返回值、多模式、线程回收的完整测试。

## 项目结构

```
mythreadpool/
├── CMakeLists.txt              # 构建脚本（生成两个可执行目标）
├── myHandWriteVersion/         # 版本一：手写版
│   ├── threadpool.h
│   ├── threadpool.cpp
│   └── using_example.cpp       # 测试示例
└── modernCppVersion/           # 版本二：Modern C++ 版（header-only）
    ├── threadpool.h
    └── using_example.cpp       # 测试示例
```

## 快速开始

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug

# 运行测试
./cmake-build-debug/myhandwrite.exe          # 版本一
./cmake-build-debug/modern_threadpool.exe    # 版本二
```

在 CLion 中重新加载 CMake 项目后，可直接选择 `myhandwrite` 或 `modern_threadpool` 运行配置。

---

## 功能总览

- **线程复用**：启动时创建一批工作线程，线程在死循环中等待任务、消费任务，避免频繁创建/销毁线程的开销
- **任务队列**：生产者（提交任务）与消费者（工作线程）通过互斥锁 + 两个条件变量（`notEmpty_` / `notFull_`）同步
- **提交背压**：任务队列满时，提交方最多阻塞等待 1 秒，超时则提交失败
- **两种模式**：
  - `MODE_FIXED`：线程数量固定
  - `MODE_CACHED`：任务多于空闲线程且未达上限时动态增加线程；空闲超过 60 秒的多余线程自动退出回收
- **优雅退出**：析构函数在锁保护下置退出标志并唤醒所有线程，等待全部线程回收后才返回（含"排空剩余任务"语义）
- **获取任务返回值**：版本一通过自实现的 `Result` + `Any` + `Semaphore`；版本二通过标准库 `std::future`

---

## 版本一：myHandWriteVersion（手写实现）

### 设计思路

用户任务继承抽象基类 `Task`，重写 `run()` 方法并返回自实现的 `Any` 类型；线程池内部用自实现的 `Semaphore` 实现返回值的线程间同步。三个核心组件全部手写，不依赖 `std::future`。

### 核心组件

| 组件 | 职责 | 关键实现 |
|---|---|---|
| `Any` | 类型擦除容器，可接收任意类型的返回值 | 基类 `Base` + 模板派生类 `Derive<T>`，`cast_<T>()` 用 `dynamic_cast` 取值；禁止拷贝、支持移动 |
| `Semaphore` | 信号量，同步"任务完成"事件 | `std::mutex` + `std::condition_variable` 实现的 `wait()` / `post()` |
| `Result` | 用户侧的任务返回值句柄 | 内部持有 `Any`（存返回值）、`Semaphore`（同步）、裸指针关联 `Task`；`get()` 阻塞直到任务执行完 |
| `Task` | 用户任务抽象基类 | `virtual Any run() = 0`；`exec()` 执行任务并经 `Result::setVal()` 回填结果、`sem_.post()` 通知 |
| `Thread` | 线程包装类 | 保存线程函数与自增 id，`start()` 中创建 `std::thread` 并 `detach()` |
| `ThreadPool` | 线程池 | 任务队列、线程表（`unordered_map<int, unique_ptr<Thread>>`）、模式与阈值控制 |

### 线程模型

```
用户线程                       工作线程 (启动时创建, detach, 死循环)
   │                                  │
   │ submitTask()                     │
   │  ├─ notFull_.wait_for(1s)        │
   │  ├─ taskQueue_.push(task)        │
   │  ├─ notEmpty_.notify_all() ─────►│ 唤醒
   │  └─ 返回 Result                  │
   │                                  │ notEmpty_.wait() 睡眠等待
   │ Result::get()                    │ 取任务 → task->exec()
   │  └─ sem_.wait() 阻塞 ────────────┤ exec() 内: result->setVal(run())
   │                                  │            └─ sem_.post() 通知
   │ 拿到 Any, cast_<T>() 取值 ◄──────┘ 继续等下一个任务
```

### 使用示例

```cpp
class MyTask : public Task {
public:
    MyTask(int id) : id_(id) {}
    Any run() override {
        // ... 耗时工作
        return some_result;   // 隐式构造 Any
    }
private:
    int id_;
};

int main() {
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_FIXED);
    pool.start(2);   // 2 个工作线程

    Result res = pool.submitTask(std::make_shared<MyTask>(1));
    int value = res.get().cast_<int>();   // 阻塞直到任务完成，取回返回值
}
```

---

## 版本二：modernCppVersion（Modern C++ 改造）

### 改造目标

不再需要继承 `Task` 基类、不再需要手写 `Any`/`Result`/`Semaphore`，**任意可调用对象**（lambda、函数指针、仿函数）连同任意数量的参数直接提交，返回标准库 `std::future`。

### 用到的 Modern C++ 特性

| 特性 | 用途 |
|---|---|
| **可变参模板 + 完美转发** | `submitTask(Func&& func, Args&&... args)` 接收任意签名、任意参数个数的任务，参数原样转发给 `std::bind` |
| **`std::packaged_task` + `std::future`** | 打包任务，自动建立"任务执行 → 返回值就绪"的同步关系，取代手写的 `Result`/`Any`/`Semaphore` |
| **返回类型自动推导** | `auto submitTask(...) -> std::future<decltype(func(args...))>`，任务返回任意类型（含 `void`、`std::string`）都能正确推导 |
| **lambda 作为任务对象** | 任务队列直接存 `std::function<void()>`，提交时把 `packaged_task` 包进 lambda 闭包 |
| **header-only** | 全部实现置于头文件，静态成员用 `inline int Thread::generateId_ = 0;`（C++17）定义，避免 ODR 问题 |
| **enum class** | 强类型的作用域枚举表达池模式 |
| **默认参数用运行时探测** | `start(int initThreadSize = std::thread::hardware_concurrency())` 默认按 CPU 核数启动 |

### 任务提交原理

```cpp
template<typename Func, typename... Args>
auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
    using RType = decltype(func(args...));
    // 1. 把可调用对象 + 参数打包成 packaged_task<RType()>
    auto task = std::make_shared<std::packaged_task<RType()>>(
        std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
    // 2. 拿到 future，任务执行完后它自动就绪
    std::future<RType> result = task->get_future();
    // 3. 队列里只存 std::function<void()>，闭包持有 shared_ptr<packaged_task>
    taskQueue_.emplace([task]() { (*task)(); });
    // 4. 通知工作线程（fixed/cached 逻辑与版本一相同）
    return result;
}
```

### 使用示例

```cpp
int main() {
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2);

    // lambda + 两个参数
    auto f1 = pool.submitTask([](int a, int b) { return a * b; }, 6, 7);
    // 普通函数
    auto f2 = pool.submitTask(add, 20, 22);
    // 无返回值任务（future<void>）
    auto f3 = pool.submitTask([]() { do_work(); });

    std::cout << f1.get();   // 42，future 语义，无需手写 Result
}
```

### 与版本一的对比

| 维度 | 版本一（手写版） | 版本二（Modern C++ 版） |
|---|---|---|
| 提交任务方式 | 继承 `Task`，重写 `run()` | 任意可调用对象 + 参数直接提交 |
| 返回值获取 | 自实现 `Result` + `Any::cast_<T>()` | 标准 `std::future<T>::get()` |
| 完成同步 | 自实现 `Semaphore` | `packaged_task` 内建的 future 共享状态 |
| 类型擦除 | 手写 `Any`（`dynamic_cast`） | 模板编译期推导 |
| 代码组织 | `.h` + `.cpp` 分离 | header-only（`inline` 静态成员） |
| 异常传播 | 不支持（`run()` 内异常需自行处理） | `get()` 时重新抛出任务中的异常 |
| 教学价值 | 理解类型擦除、信号量、条件变量的底层实现 | 理解可变参模板、完美转发、future 异步编程 |
