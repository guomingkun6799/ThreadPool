//
// Created by MingkunGuo on 2026/10/8.
//

#ifndef MYTHREADPOOL_THREADPOOL_H
#define MYTHREADPOOL_THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

//Any类型：可以接收任意数据类型
class Any {
public:
    Any() = default;
    ~Any() = default;
    Any(const Any &) = delete;
    Any &operator=(const Any &) = delete;
    Any(Any &&) = default;
    Any &operator=(Any &&) = default;

    //这个构造函数可以让Any类型接收任意其他数据
    template <typename T>
    Any(T data) :base_(std::make_unique<Derive<T>>(data)) {}

    //这个方法可以把Any里面的对象提取出来
    template <typename T>
    T cast_() {
        //从base_找到所指向的Derive对象，从它里面取出data
        //基类指针转成派生类指针
        Derive<T> *pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) {
            throw "type is unmatch!";
        }
        return pd -> data_;
    }
private:
    //基类类型
    class Base {
    public:
        virtual ~Base() = default;
    };

    //派生类型
    template <typename T>
    class Derive : public Base {
    public:
        Derive(T data): data_(data) {}
        T data_; //保存了其他类型
    };
private:
    std::unique_ptr<Base> base_;
};

//实现一个信号量类
class Semaphore {
public:
    Semaphore(int limit = 0) :
        resLimit_(limit)
    {}
    ~Semaphore() = default;

    //获取一个信号资源
    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        //等待信号量有资源，没有资源就阻塞
        cond_.wait(lock, [&]() -> bool {return resLimit_ > 0;});
        resLimit_--;
    }

    //增加一个信号资源
    void post() {
        std::unique_lock<std::mutex> lock(mutex_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

class Result;

//任务抽象基类
//用户可以自定义任意任务类型，从Task继承，重写run方法
class Task {
public:
    Task();
    ~Task() = default;
    void exec();
    void setResult(Result* result);
    virtual Any run() = 0;
private:
    Result* result_; //Result对象的生命周期强于task
};

//实现接收提交到线程池task任务执行完成后的返回值
class Result {
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    //setVal方法，获取任务执行完的返回值的
    void setVal(Any any);
    //get方法，用户调用这个方法获取task的返回值
    Any get();

private:
    Any any_; //存储任务的返回值
    Semaphore sem_; //线程通信信号量
    std::shared_ptr<Task> task_; //指向对应获取返回值的任务对象
    std::atomic_bool isValid_; //返回值是否有效
};

// 线程池支持的模式
enum class PoolMode {
    MODE_FIXED, //固定数量的线程
    MODE_CACHED, // 线程数量可动态增长
};

//线程类型
class Thread {
public:
    //线程函数对象类型
    using ThreadFunc = std::function<void(int)>;

    //线程构造
    Thread(ThreadFunc func);
    //线程析构
    ~Thread();
    //启动线程
    void start();

    //获取线程id
    int getId()const;
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; //保存线程id
};

/**
example:
ThreadPool pool;
pool.start(4);

class MyTask : public task{
public:
    void run() { // 线程代码...}
}；

pool.submitTask(std::make_shared<MyTask>());
*/
// 线程池类型
class ThreadPool {
public:
    //线程池构造
    ThreadPool();
    //线程池析构
    ~ThreadPool();

    void setMode(PoolMode mode); //设置线程池模式

    void setTaskQueMaxThreshhold(int taskQueMaxThreshhold); //设置task任务队列上线阈值

    void setThreadSizeThreshhold(int threadSizeThreshhold);

    Result submitTask(std::shared_ptr<Task> sp); //给线程池提交任务

    void start(int initThreadSize = 4); //开启线程池
    //禁止线程池的拷贝和赋值构造
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
private:
    //定义线程函数
    void threadFunc(int threadId);
    bool checkRunningState() const;
private:
    //std::vector<std::unique_ptr<Thread>> threads_; //线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; //线程列表
    int initThreadSize_; //初始的线程数量
    std::atomic_int currentThreadSize_; //记录当前线程池中线程的总数量
    std::atomic_int idleThreadsSize_; //记录空闲线程数量
    int threadSizeThreshhold_; //线程数量的上线,cache模式用

    std::queue<std::shared_ptr<Task>> taskQueue_; //任务队列
    std::atomic_int taskSize_; //任务的数量
    int taskQueMaxThreshhold_; //任务队列阈值

    std::mutex taskQueMtx_; //保证任务队列的线程安全
    std::condition_variable notFull_; //表示任务队列不满
    std::condition_variable notEmpty_; //表示任务队列不空

    PoolMode poolMode_; //当前线程池的工作模式

    //表示当前线程池的启动状态
    std::atomic_bool isPoolRunning_;
};

#endif //MYTHREADPOOL_THREADPOOL_H
