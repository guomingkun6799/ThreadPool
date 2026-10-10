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
#include <thread>
#include <future>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;
const int ThreadMax = 10;
const int THREAN_MAX_IDLE_TIME = 60;

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
    Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++)
    {}
    //线程析构
    ~Thread() = default;
    //启动线程
    void start() {
        //创建一个线程来执行一个线程函数
        std::thread t(func_, threadId_); //C++11来说，线程对象t和线程函数func_
        t.detach(); //设置分离线程
    }

    //获取线程id
    int getId()const {
        return threadId_;
    }
private:
    ThreadFunc func_;
    static int generateId_;
    int threadId_; //保存线程id
};

inline int Thread::generateId_ = 0;

// 线程池类型
class ThreadPool {
public:
    //线程池构造
    //线程池构造
    ThreadPool()
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
    ~ThreadPool() {
        //在锁的保护下修改退出标志并唤醒所有等待的线程，防止通知丢失
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        isPoolRunning_ = false;
        notEmpty_.notify_all();
        //等待线程池里面所有线程返回 有两种状态：阻塞 & 正在执行任务中
        exitCond_.wait(lock, [&]() -> bool {return threads_.size() == 0;});
    }

    void setMode(PoolMode mode) {
        if (checkRunningState()) {
            return;
        }
        poolMode_ = mode;
    }

    //设置task任务队列上线阈值
    void setTaskQueMaxThreshhold(int threshhold) {
        if (checkRunningState()) {
            return;
        }
        taskQueMaxThreshhold_ = threshhold;
    }

    void setThreadSizeThreshhold(int threadSizeThreshhold) {
        if (checkRunningState() && poolMode_ == PoolMode::MODE_FIXED) {
            return;
        }
        threadSizeThreshhold_ = threadSizeThreshhold;
    }

    //使用可变参模板编程，让submitTask可以接收任意任务函数以及任意数量的参数
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
        //打包任务，放入任务队列
        using  RType = decltype(func(args...));
        auto task = std::make_shared<std::packaged_task<RType()>>(
              std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );
        std::future<RType> result = task->get_future();

        std::unique_lock<std::mutex> lock(taskQueMtx_);
        // 线程的通信，通过信号量等待任务队列有空余
        //用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败
        if (!notFull_.wait_for(lock, std::chrono::seconds(1)
            ,[&]() -> bool { return taskQueue_.size() < taskQueMaxThreshhold_; })) {
            //表示notFull_等待1s，条件仍然没满足
            std::cerr << "task queue is full, submit task fail." << std::endl;
            auto dummy = std::make_shared<std::packaged_task<RType()>>(
                []()->RType {return RType();}
            );
            //同步执行哑任务，使 future 立即就绪并返回默认值（否则 task 析构后 get() 会抛 broken_promise）
            (*dummy)();
            return dummy -> get_future();
        }
        // 如果有空余，把任务放入任务队列中
        taskQueue_.emplace([task]() {
            //去执行下面的任务
            (*task)();
        });
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
        return result;
    }

    //开启线程池
    void start(int initThreadSize) {

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
    //禁止线程池的拷贝和赋值构造
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
private:
    //定义线程函数
    void threadFunc(int threadid) {
        auto lastTime = std::chrono::high_resolution_clock().now();

        //所有任务必须执行完成，线程池才可以回收所有线程资源
        for (;;) {
            Task task;
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
                                //通知析构函数：线程资源已全部回收
                                exitCond_.notify_all();
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
                task();
            }
            idleThreadsSize_++;
            lastTime = std::chrono::high_resolution_clock::now(); //更新线程执行完任务的时间
        }
    }
    bool checkRunningState() const {
        return isPoolRunning_;
    }
private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_; //线程列表

    int initThreadSize_; //初始的线程数量
    std::atomic_int currentThreadSize_; //记录当前线程池中线程的总数量
    std::atomic_int idleThreadsSize_; //记录空闲线程数量
    int threadSizeThreshhold_; //线程数量的上线,cache模式用

    using Task = std::function<void()>;
    std::queue<Task> taskQueue_; //任务队列
    std::atomic_int taskSize_; //任务的数量
    int taskQueMaxThreshhold_; //任务队列阈值

    std::mutex taskQueMtx_; //保证任务队列的线程安全
    std::condition_variable notFull_; //表示任务队列不满
    std::condition_variable notEmpty_; //表示任务队列不空
    std::condition_variable exitCond_; //等待线程资源全部回收

    PoolMode poolMode_; //当前线程池的工作模式
    std::atomic_bool isPoolRunning_;//表示当前线程池的启动状态
};

#endif //MYTHREADPOOL_THREADPOOL_H
