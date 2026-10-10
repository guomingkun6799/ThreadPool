//
// Created by MingkunGuo on 2026/10/8.
//

#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;
const int ThreadMax = 10;
const int THREAN_MAX_IDLE_TIME = 60;

//线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(4)
    , taskSize_(0)
    , taskQueMaxThreshhold_(TASK_MAX_THRESHHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
    , idleThreadsSize_(0)
    , threadSizeThreshhold_(ThreadMax)
    , currentThreadSize_(0)
{}

//线程池析构
ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;
    //等待线程池里面所有线程返回 有两种状态：阻塞 & 正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]() -> bool {return threads_.size() == 0;});
}

//设置线程池工作模式
void ThreadPool::setMode(PoolMode mode) {
    if (checkRunningState()) {
        return;
    }
    poolMode_ = mode;
}

//设置task任务队列上线阈值
void ThreadPool::setTaskQueMaxThreshhold(int threshhold) {
    if (checkRunningState()) {
        return;
    }
    taskQueMaxThreshhold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshhold(int threadSizeThreshhold) {
    if (checkRunningState() && poolMode_ == PoolMode::MODE_FIXED) {
        return;
    }
    threadSizeThreshhold_ = threadSizeThreshhold;
}

//给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
    // 获取锁,生产者和消费者消费同一个队列需要线程互斥
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    // 线程的通信，通过信号量等待任务队列有空余
    //用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败
    if (!notFull_.wait_for(lock, std::chrono::seconds(1)
        ,[&]() -> bool { return taskQueue_.size() < taskQueMaxThreshhold_; })) {
        //表示notFull_等待1s，条件仍然没满足
        std::cerr << "task queue is full, submit task fail." << std::endl;
        return Result(sp,false); //result不能task -> getResult，因为task执行完会被pop被析构
    }
    // 如果有空余，把任务放入任务队列中
    taskQueue_.emplace(sp);
    taskSize_++;
    // 因为新放了任务，任务队列肯定不空了，notEmpty通知,分配线程执行任务
    notEmpty_.notify_all();

    //cached模式，需要根据任务数量/空闲线程数量，判断是否幼新建数量
    if (poolMode_ == PoolMode::MODE_CACHED
        && taskSize_ > idleThreadsSize_
        && currentThreadSize_ < threadSizeThreshhold_) {
        //创建新线程入池
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[threadId] -> start();
        //修改线程个数相关变量
        currentThreadSize_++;
        idleThreadsSize_++;
    }

    //返回任务的Result对象
    return Result(sp);
}

//开启线程池
void ThreadPool::start(int initThreadSize) {

    isPoolRunning_ = true;
    //记录初始线程个数
    initThreadSize_ = initThreadSize;
    currentThreadSize_ = initThreadSize;

    //创建线程对象并启动
    for (int i = 0; i < initThreadSize_; ++i) {
        //创建thread线程对象的时候，把线程函数给到thread对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[threadId]->start(); //需要去执行一个线程函数
        idleThreadsSize_++; //记录初始空闲线程的数量
    }
}

//定义线程函数 线程池所有线程从任务队列里消费任务
void ThreadPool::threadFunc(int threadid) {

    auto lastTime = std::chrono::high_resolution_clock().now();

    //所有任务必须执行完成，线程池才可以回收所有线程资源
    for (;;) {
        std::shared_ptr<Task> task;
        //区别作用域，把锁及时释放掉
        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            //每一秒钟返回一次
            //锁加双重判断
            while (taskQueue_.size() == 0) {
                if (!isPoolRunning_) {
                    //回收执行完任务的线程
                    threads_.erase(threadid);
                    exitCond_.notify_all();
                    std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
                    return; //线程函数结束，线程结束
                }
                if (poolMode_ == PoolMode::MODE_CACHED) {
                    //条件变量超时返回
                    if (std::cv_status::timeout ==
                        notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAN_MAX_IDLE_TIME
                            && currentThreadSize_ > initThreadSize_) {
                            //开始回收当前线程
                            //记录线程数量的相关遍历的数值修改
                            threads_.erase(threadid);
                            currentThreadSize_--;
                            idleThreadsSize_--;
                            return;
                        }
                    }
                }else {
                    // 等待notEmpty
                    notEmpty_.wait(lock);
                }
            }
            // 取一个任务队列中取一个任务出来
            task = taskQueue_.front();
            taskQueue_.pop();
            taskSize_--;
            idleThreadsSize_--;
            //如果依然有剩余任务，继续通知其他线程执行任务
            if (taskQueue_.size() > 0) {
                notFull_.notify_all();
            }

            //取出一个任务，进行通知
            notFull_.notify_all();
        }
        // 当前线程负责执行这个任务
        if (task != nullptr) {
            task -> exec();
        }
        idleThreadsSize_++;
        lastTime = std::chrono::high_resolution_clock::now(); //更新线程执行完任务的时间
    }
}

bool ThreadPool::checkRunningState() const {
    return isPoolRunning_;
}

//--------------------- 线程方法实现-----------------------------
int Thread::generateId_ = 0;
//线程构造
Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)
{}

//线程析构
Thread::~Thread() {}

//启动线程
void Thread::start() {
    //创建一个线程来执行一个线程函数
    std::thread t(func_, threadId_); //C++11来说，线程对象t和线程函数func_
    t.detach(); //设置分离线程
}

int Thread::getId()const {
    return threadId_;
};

//--------------------- task方法实现-----------------------------
Task::Task()
    :result_(nullptr)
{}

void Task::exec() {
    if (result_ != nullptr) {
        result_ ->setVal(run());
    }
}

void Task::setResult(Result *result) {
    result_ = result;
}

//--------------------- result方法实现-----------------------------
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task)
    , isValid_(isValid) {
    task_ ->setResult(this);
}

Any Result::get() {
    if (!isValid_) {
        return "";
    }
    sem_.wait(); //task如果没有执行完，这里会阻塞用户线程
    return std::move(any_);
}

void Result::setVal(Any any) {
    //存储task的返回数值
    this ->any_ = std::move(any);
    sem_.post(); //已经获取任务的返回值，增加信号量资源
}
