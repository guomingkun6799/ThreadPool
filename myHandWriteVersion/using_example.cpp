//
// Created by MingkunGuo on 2026/10/9.
//
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>

#include "threadpool.h"

//测试任务：模拟耗时工作，返回 1+2+...+100 的计算结果
class MyTask : public Task {
public:
    explicit MyTask(int id) : id_(id) {}

    Any run() {
        std::cout << "task " << id_ << " start, tid: "
                  << std::this_thread::get_id() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        int sum = 0;
        for (int i = 1; i <= 100; ++i) {
            sum += i;
        }
        std::cout << "task " << id_ << " end" << std::endl;
        return sum;
    }
private:
    int id_;
};

int main() {
    const int expect = 5050;
    bool allPass = true;

    //---------------- 测试1：fixed 模式 + 获取任务返回值 ----------------
    {
        std::cout << "===== test1: fixed mode =====" << std::endl;
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_FIXED);
        pool.start(2);

        for (int i = 1; i <= 4; ++i) {
            Result res = pool.submitTask(std::make_shared<MyTask>(i));
            //get() 会阻塞直到任务执行完，然后通过 Any 取出返回值
            int sum = res.get().cast_<int>();
            bool pass = (sum == expect);
            allPass = allPass && pass;
            std::cout << "main: task " << i << " result = " << sum
                      << (pass ? " [PASS]" : " [FAIL]") << std::endl;
        }
    } //pool 析构：验证所有线程能被正常回收，程序不挂死

    //---------------- 测试2：cached 模式 + 动态扩容 ----------------
    {
        std::cout << "===== test2: cached mode =====" << std::endl;
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.setThreadSizeThreshhold(10);
        pool.start(2);

        for (int i = 1; i <= 8; ++i) {
            Result res = pool.submitTask(std::make_shared<MyTask>(i));
            int sum = res.get().cast_<int>();
            bool pass = (sum == expect);
            allPass = allPass && pass;
            std::cout << "main: task " << i << " result = " << sum
                      << (pass ? " [PASS]" : " [FAIL]") << std::endl;
        }
    }

    if (allPass) {
        std::cout << "all tests passed." << std::endl;
    } else {
        std::cout << "some tests FAILED." << std::endl;
    }
    return 0;
}
