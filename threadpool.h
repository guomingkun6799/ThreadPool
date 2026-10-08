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
};
// 线程池类型
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();

    void setMode(PoolMode mode);

    void start(); //开启线程池
private:
    std::vector<Thread*> threads_; //线程列表
    size_t initThreadSize_; //初始的线程数量

    std::queue<std::shared_ptr<Task>> taskQueue_; //任务队列
    std::atomic_int taskSize_; //任务的数量
    int taskQueMaxThreshhold_; //任务队列阈值

    std::mutex taskQueMtx_; //保证任务队列的线程安全
    std::condition_variable notFull_; //表示任务队列不满
    std::condition_variable notEmpty_; //表示任务队列不空

    PoolMode poolMode_; //当前线程池的工作模式
};

#endif //MYTHREADPOOL_THREADPOOL_H
