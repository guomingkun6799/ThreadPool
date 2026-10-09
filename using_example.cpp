//
// Created by ma-te on 2026/10/9.
//
#include <iostream>
#include <chrono>
#include <thread>
using namespace std;

#include "threadpool.h"

class MyTask : public Task {
public:
    void run() {
        std::cout << "tid:" << std::this_thread::get_id()<<"Running task" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "tid:" << std::this_thread::get_id()<<"Task end" << std::endl;
    }
};

int main() {
    ThreadPool pool;
    pool.start(2);

    for (int i = 0; i < 4; i++) {
        pool.submitTask(std::make_shared<MyTask>());
    }

    getchar();

    return 0;
}