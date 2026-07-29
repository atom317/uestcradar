#include <chrono>
#include <iostream>
#include <thread>

int main() {
    while (true) {
        std::cout << "Hello, World!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
