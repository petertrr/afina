#include "Worker.h"

#include <iostream>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

//#include <errno.h>
#include <cstring>
#include <string>
#include <algorithm>

#include "Utils.h"
#include <afina/execute/Command.h>
#include <../src/protocol/Parser.h>

namespace Afina {
namespace Network {
namespace NonBlocking {
    
// See Worker.h
Worker::Worker(std::shared_ptr<Afina::Storage> ps) : pStorage(ps)
{ }

Worker::Worker(Worker&& w) :
    pStorage(w.pStorage)
    , thread(w.thread)
    , server_socket(w.server_socket)
{
    std::cout << "Entering move constructor\n";
    running.store(w.running.load());
}

// See Worker.h
Worker::~Worker() {
    // TODO: implementation here
}

// See Worker.h
void Worker::Start(int server_socket) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: implementation here

    this->server_socket = server_socket;
    running.store(true);
    struct args_t *args = (struct args_t*)malloc(sizeof(struct args_t));
    args->socket = server_socket;
    args->this_ptr = this;
    pthread_create(&thread, NULL, OnRunWrapper, (void*)args);
}

// See Worker.h
void Worker::Stop() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: implementation here
    running.store(false);
    shutdown(server_socket, SHUT_RDWR);
}

// See Worker.h
void Worker::Join() {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    // TODO: implementation here
    pthread_join(thread, 0);
}

void *Worker::OnRunWrapper(void *args) {
    args_t *wa = (args_t*)args;
    Worker* worker = reinterpret_cast<Worker *>(wa->this_ptr);
    worker->OnRun(args);
}

// See Worker.h
void Worker::OnRun(void *args) {
    std::cout << "network debug: " << __PRETTY_FUNCTION__ << std::endl;
    args_t *wa = (args_t*)args;
    int server_socket = wa->socket;

    // TODO: implementation here
    // 1. Create epoll_context here
    int epoll_fd = epoll_create(200);
    if (-1 == epoll_fd) {
        throw std::runtime_error("Failed to create epoll context.");
    }

    // 2. Add server_socket to context
    struct epoll_event server_listen_event;
    server_listen_event.events = EPOLLIN | EPOLLET | EPOLLEXCLUSIVE;
    server_listen_event.data.ptr = (void*)&server_socket;
    if (-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &server_listen_event)) {
        throw std::runtime_error("Failed to add an event for server socket.");
    }

    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    unsigned int MAXEVENTS = 20;
    struct epoll_event *events = (epoll_event*)calloc(MAXEVENTS, sizeof(struct epoll_event));
    bool need_exit = false;
    while( running.load() )
    {
        int n = epoll_wait(epoll_fd, events, MAXEVENTS, -1);
        if (-1 == n) {
            throw std::runtime_error("Failed to epoll_wait");
        }

        for (int i = 0; i < n; ++i) {
            if( &server_socket == events[i].data.ptr ) {
                // 3. Accept new connections, don't forget to call make_socket_nonblocking on
                //    the client socket descriptor
                //std::cout << "Processing accept in thread " << thread << std::endl;
                int client_socket = accept(server_socket, (struct sockaddr *)&clientaddr, &clientlen);
                if (-1 == client_socket) {
                    if( errno == EINVAL || errno == EAGAIN || errno == EWOULDBLOCK ) {
                        need_exit = true;
                        break;
                    } else {
                        throw std::runtime_error("Accept failed");
                    }
                }
                if( need_exit )
                    break;
                make_socket_non_blocking(client_socket);
            
                // 4. Add connections to the local context
                //
                // Do not forget to use EPOLLEXCLUSIVE flag when register socket
                // for events to avoid thundering herd type behavior.
                struct epoll_event client_conn_event;
                client_conn_event.events = EPOLLIN | EPOLLEXCLUSIVE;
                client_conn_event.data.ptr = (void*)&client_socket;
                if (-1 == epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &client_conn_event)) {
                    throw std::runtime_error("Failed to add an EPOLLIN event for client socket.");
                }
            } else if( EPOLLHUP & events[i].events || EPOLLERR & events[i].events ) {
                close(*(int*)events[i].data.ptr);
            } else {
                // 5. Process connection events
                //std::cout << "Processing connection in thread " << thread << std::endl;
                int client_socket = *(int*)events[i].data.ptr;
                const size_t buf_size = 256;
                char msg_buf[buf_size];
                memset(msg_buf, 0, buf_size);
                int rval;
                size_t parsed = 0;
                Afina::Protocol::Parser parser;
                std::string command = "";
                while( (rval = read(client_socket, msg_buf, buf_size)) != 0 || command.size() > 0 )
                {
                    command += msg_buf;
                    //if( rval > 0 )
                    //    std::cout << "Command string : " << command << "\n";
                    bool parse_finished = false;
                    try {
                        parse_finished = parser.Parse(command, parsed);
                    } catch(...) {
                        std::string result = "ERROR\r\n";
                        if( send(client_socket, result.data(), result.size(), 0) <= 0 ) {
                            close(client_socket);
                            throw std::runtime_error("Socket send() failed");
                        }
                        break;
                    }
                    command.erase(0, parsed);
                    if( parse_finished ) {
                        uint32_t body_size;
                        std::unique_ptr<Afina::Execute::Command> com_ptr = parser.Build(body_size);
                        std::string args;
                        if( body_size > 0 ) {
                            // get command argument
                            while( body_size + 2 > command.size() ) {
                                rval = read(client_socket, msg_buf, buf_size);
                                command += msg_buf;
                                memset(msg_buf, 0, buf_size);
                            }
                            args = command.substr(0, body_size);
                            command.erase(0, body_size + 2); // including /r/n
                        }
                        std::string result;
                        try {
                            com_ptr->Execute(*pStorage, args, result);
                        //    std::cout << "Running command in thread " << thread << std::endl;
                        } catch(...) {
                            result = "SERVER_ERROR";
                        }
                        result += "\r\n";
                        if ( result.size() && send(client_socket, result.data(), result.size(), 0) <= 0 ) {
                            close(client_socket);
                            throw std::runtime_error("Socket send() failed");
                        }
                        //std::cout << "Sent result " << result << "\n";
                        parser.Reset();
                        parse_finished = false;
                        parsed = 0;
                    }
                    memset(msg_buf, 0, buf_size);
                }
                close(client_socket);
                //std::cout << "Closing socket and leaving this connection\n";
            }
        }
    }
    pthread_exit(0);
}

} // namespace NonBlocking
} // namespace Network
} // namespace Afina
