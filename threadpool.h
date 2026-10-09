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
//任务抽象基类
//用户可以自定义任意任务类型，从Task继承，重写run方法
class Task {
public:
    virtual  void run() = 0;
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
    using ThreadFunc = std::function<void()>;

    //线程构造
    Thread(ThreadFunc func);
    //线程析构
    ~Thread();
    //启动线程
    void start();
private:
    ThreadFunc func_;
};

// 线程池类型
class ThreadPool {
public:
    //线程池构造
    ThreadPool();
    //线程池析构
    ~ThreadPool();

    void setMode(PoolMode mode); //设置线程池模式

    void setTaskQueMaxThreshhold(int taskQueMaxThreshhold); //设置task任务队列上线阈值

    void submitTask(std::shared_ptr<Task> sp); //给线程池提交任务

    void start(int initThreadSize = 4); //开启线程池
    //禁止线程池的拷贝和赋值构造
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
private:
    //定义线程函数
    void threadFunc();
private:
    std::vector<std::unique_ptr<Thread>> threads_; //线程列表
    int initThreadSize_; //初始的线程数量

    std::queue<std::shared_ptr<Task>> taskQueue_; //任务队列
    std::atomic_int taskSize_; //任务的数量
    int taskQueMaxThreshhold_; //任务队列阈值

    std::mutex taskQueMtx_; //保证任务队列的线程安全
    std::condition_variable notFull_; //表示任务队列不满
    std::condition_variable notEmpty_; //表示任务队列不空

    PoolMode poolMode_; //当前线程池的工作模式
};

#endif //MYTHREADPOOL_THREADPOOL_H
