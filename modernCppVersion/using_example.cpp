//
// Created by ma-te on 2026/10/10.
//
#include <iostream>
#include <chrono>
#include <thread>
#include <string>

#include "threadpool.h"

//普通函数作为任务
int add(int a, int b) {
    return a + b;
}

int main() {
    bool allPass = true;
    auto check = [&allPass](const std::string& name, bool pass) {
        allPass = allPass && pass;
        std::cout << name << (pass ? " [PASS]" : " [FAIL]") << std::endl;
    };

    //---------------- 测试1：fixed 模式，多种任务形态 ----------------
    {
        std::cout << "===== test1: fixed mode =====" << std::endl;
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_FIXED);
        pool.start(2);

        //1) lambda + 两个参数，返回 int
        auto f1 = pool.submitTask([](int a, int b) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return a * b;
        }, 6, 7);
        check("lambda task: 6 * 7 == 42", f1.get() == 42);

        //2) 普通函数 + 参数
        auto f2 = pool.submitTask(add, 20, 22);
        check("free function: add(20, 22) == 42", f2.get() == 42);

        //3) 无返回值任务（future<void>）
        auto f3 = pool.submitTask([]() {
            std::cout << "void task running..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
        f3.get(); //等待执行完成即可
        check("void task completed", true);

        //4) 返回字符串的任务，验证 Any 之外的类型推导
        auto f4 = pool.submitTask([](const std::string& name) {
            return "hello, " + name;
        }, std::string("threadpool"));
        check("string task: hello, threadpool", f4.get() == "hello, threadpool");
    } //pool 析构：验证所有线程能被正常回收，程序不挂死

    //---------------- 测试2：cached 模式 + 动态扩容 ----------------
    {
        std::cout << "===== test2: cached mode =====" << std::endl;
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);
        pool.setThreadSizeThreshhold(10);
        pool.start(2);

        for (int i = 1; i <= 4; ++i) {
            auto f = pool.submitTask([i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return i * i;
            });
            check("cached task: " + std::to_string(i) + "^2", f.get() == i * i);
        }
    }

    if (allPass) {
        std::cout << "all tests passed." << std::endl;
    } else {
        std::cout << "some tests FAILED." << std::endl;
    }
    return 0;
}
