#include "application/ipc/server.hpp"

#include <chrono>
#include <iostream>
#include <thread>

/** Device-free real-socket fixture: publish a fixed observation at 1kHz for bounded contract checks. */
int main(int argc, char* argv[]) {
    if (argc != 2)
        return 2;
    try {
        robot_control::application::ipc::Server server{argv[1], 5};
        robot_control::application::ipc::Snapshot snapshot{.left_feedback_tenths_rpm = -2, .healthy = true};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
        std::cout << "READY\n" << std::flush;
        while (std::chrono::steady_clock::now() < deadline) {
            ++snapshot.cycles;
            server.poll(snapshot);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
