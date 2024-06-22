#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::thread t1([] {
        for (int i = 0; i < 10; ++i) {
            std::cout << "Thread 1: " << i << std::endl;
            std::this_thread::sleep_for(10ms);
        }
    });

    std::thread t2([] {
        for (int i = 0; i < 10; ++i) {
            std::cout << "Thread 2: " << i << std::endl;
            std::this_thread::sleep_for(10ms);
        }
    });

    t1.join();
    t2.join();
    return 0;
}
