//
// Created by MingkunGuo on 2026/10/8.
//

#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;

//线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(4)
    , taskSize_(0)
    , taskQueMaxThreshhold_(TASK_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
{}

//线程池析构
ThreadPool::~ThreadPool() {}

//设置线程池工作模式
void ThreadPool::setMode(PoolMode mode) {
    poolMode_ = mode;
}

//设置task任务队列上线阈值
void ThreadPool::setTaskQueMaxThreshhold(int threshhold) {
    taskQueMaxThreshhold_ = threshhold;
}

//给线程池提交任务
void ThreadPool::submitTask(std::shared_ptr<Task> sp) {
    // 获取锁,生产者和消费者消费同一个队列需要线程互斥
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // 线程的通信，通过信号量等待任务队列有空余
    notFull_.wait(lock, [&]() -> bool { return taskQueue_.size() < taskQueMaxThreshhold_; });
    // 如果有空余，把任务放入任务队列中
    taskQueue_.emplace(sp);
    taskSize_++;
    // 因为新放了任务，任务队列肯定不空了，notEmpty通知,分配线程执行任务
    notEmpty_.notify_all();
}

//开启线程池
void ThreadPool::start(int initThreadSize) {
    //记录初始线程个数
    initThreadSize_ = initThreadSize;

    //创建线程对象
    for (int i = 0; i < initThreadSize_; ++i) {
        //创建thread线程对象的时候，把线程函数给到thread对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
        threads_.emplace_back(std::move(ptr));
    }

    //启动所有线程
    for (int i = 0; i < initThreadSize_; ++i) {
        threads_[i]->start(); //需要去执行一个线程函数
    }
}

void ThreadPool::threadFunc() {
    std::cout << "begin threadFunc tid:"<<std::this_thread::get_id() << std::endl;
    std::cout << "end threadFunc" << std::endl;

}

//--------------------- 线程方法实现-----------------------------

//线程构造
Thread::Thread(ThreadFunc func)
    : func_(func)
{}

//线程析构
Thread::~Thread() {}

//启动线程
void Thread::start() {
    //创建一个线程来执行一个线程函数
    std::thread t(func_); //C++11来说，线程对象t和线程函数func_
    t.detach(); //设置分离线程
}
