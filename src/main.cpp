#include <chrono>
#include <iostream>
#include <memory>
//#include <uv.h>
#include <fstream>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include <cxxopts.hpp>

#include <afina/Storage.h>
#include <afina/Version.h>
#include <afina/network/Server.h>

#include "network/blocking/ServerImpl.h"
#include "network/nonblocking/ServerImpl.h"
#include "network/uv/ServerImpl.h"
#include "storage/MapBasedGlobalLockImpl.h"

typedef struct {
    std::shared_ptr<Afina::Storage> storage;
    std::shared_ptr<Afina::Network::Server> server;
} Application;

int main(int argc, char **argv) {
    // Build version
    // TODO: move into Version.h as a function
    std::stringstream app_string;
    app_string << "Afina " << Afina::Version_Major << "." << Afina::Version_Minor << "." << Afina::Version_Patch;
    if (Afina::Version_SHA.size() > 0) {
        app_string << "-" << Afina::Version_SHA;
    }

    // Command line arguments parsing
    cxxopts::Options options("afina", "Simple memory caching server");
    try {
        // TODO: use custom cxxopts::value to print options possible values in help message
        // and simplify validation below
        options.add_options()("s,storage", "Type of storage service to use", cxxopts::value<std::string>());
        options.add_options()("n,network", "Type of network service to use", cxxopts::value<std::string>());
        options.add_options()("h,help", "Print usage info");
        options.add_options()("d,daemon", "Run server as a daemon");
        options.add_options()("p,pid", "Write PID to file", cxxopts::value<std::string>());
        options.parse(argc, argv);

        if (options.count("help") > 0) {
            std::cerr << options.help() << std::endl;
            return 0;
        }
        if( options.count("daemon") > 0 ) {
            pid_t p = fork();
            if( p == 0 ) {
                setsid();
                close(STDIN_FILENO);
                close(STDOUT_FILENO);
                close(STDERR_FILENO);
            } else {
                return 0;
            }
        }
        if( options.count("pid") > 0 ) {
            std::string filename = options["pid"].as<std::string>();
            pid_t pid = getpid();
            std::ofstream fout;
            fout.open(filename.c_str());
            fout << pid;
            fout.close();
        }
    } catch (cxxopts::OptionParseException &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    // Start boot sequence
    Application app;
    std::cout << "Starting " << app_string.str() << std::endl;

    // Build new storage instance
    std::string storage_type = "map_global";
    if (options.count("storage") > 0) {
        storage_type = options["storage"].as<std::string>();
    }

    if (storage_type == "map_global") {
        app.storage = std::make_shared<Afina::Backend::MapBasedGlobalLockImpl>();
    } else {
        throw std::runtime_error("Unknown storage type");
    }

    // Build  & start network layer
    std::string network_type = "uv";
    if (options.count("network") > 0) {
        network_type = options["network"].as<std::string>();
    }

    if (network_type == "uv") {
        app.server = std::make_shared<Afina::Network::UV::ServerImpl>(app.storage);
    } else if (network_type == "blocking") {
        app.server = std::make_shared<Afina::Network::Blocking::ServerImpl>(app.storage);
    } else if (network_type == "nonblocking") {
        app.server = std::make_shared<Afina::Network::NonBlocking::ServerImpl>(app.storage);
    } else {
        throw std::runtime_error("Unknown network type");
    }

    // Init local loop. It will react to signals and performs some metrics collections. Each
    // subsystem is able to push metrics actively, but some metrics could be collected only
    // by polling, so loop here will does that work
    int loop_epoll_fd = epoll_create(200);

    // Init stop signal handlers
    sigset_t stop_flag;
    sigemptyset(&stop_flag);
    sigaddset(&stop_flag, SIGTERM);
    sigaddset(&stop_flag, SIGKILL);
    sigaddset(&stop_flag, SIGINT);
    if( sigprocmask(SIG_BLOCK, &stop_flag, NULL) == -1 )
        throw std::runtime_error("Failed to sigprocmask in main event loop");
    int stop_sig_fd = signalfd(-1, &stop_flag, 0);
    if( stop_sig_fd == -1 )
        throw std::runtime_error("Failed to create signalfd in main event loop");

    // Add signal handling to epoll context
    epoll_event sig_event;
    sig_event.data.ptr = &stop_sig_fd;
    sig_event.events = EPOLLIN;
    epoll_ctl(loop_epoll_fd, EPOLL_CTL_ADD, stop_sig_fd, &sig_event);

    // Create timer for periodic debug information
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if( timer_fd < 0)
        throw std::runtime_error("Failed to create timer in main event loop");
    struct itimerspec new_value;
    new_value.it_interval.tv_sec = 5;
    new_value.it_interval.tv_nsec = 0;
    new_value.it_value.tv_sec = 5;
    new_value.it_value.tv_nsec = 0;
    if( -1 == timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &new_value, NULL) )
        throw std::runtime_error("Failed to set time on timer fd");

    // Add timer fd to epoll context
    epoll_event time_event;
    time_event.data.ptr = &timer_fd;
    time_event.events = EPOLLIN | EPOLLET;
    epoll_ctl(loop_epoll_fd, EPOLL_CTL_ADD, timer_fd, &time_event);

    // Start services
    try {
        app.storage->Start();
        app.server->Start(8080, 10);

        std::cout << "Application started" << std::endl;
        const int MAXEVENTS = 8;
        struct epoll_event *loop_events = (struct epoll_event*)calloc(MAXEVENTS, sizeof(struct epoll_event));
        bool loop_running = true;
        while( loop_running )
        {
            int n = epoll_wait(loop_epoll_fd, loop_events, MAXEVENTS, -1);
            if( n < 0)
                throw std::runtime_error("Failed to epoll_wait in main event loop");
            for(int i = 0; i < n; ++i)
            {
                if( &stop_sig_fd == loop_events[i].data.ptr ) {
                    std::cout << "Receive stop signal" << std::endl;
                    loop_running = false;
                } else if( &timer_fd == loop_events[i].data.ptr ) {
                        uint64_t val = 0;
                        int rval = read(timer_fd, &val, sizeof(uint64_t));
                        if( rval > 0 ) {
                            std::cout << "Start passive metrics collection" << std::endl;
                    }
                }
            }
        }
        free(loop_events);

        // Stop services
        app.server->Stop();
        app.server->Join();
        app.storage->Stop();

        std::cout << "Application stopped" << std::endl;
    } catch (std::exception &e) {
        std::cerr << "Fatal error" << e.what() << std::endl;
    }

    return 0;
}
