#include "../../include/afina/Executor.h"
//#include <afina/Executor.h>

namespace Afina
{
void perform(Executor *executor) {
    while( true ) {
        std::unique_lock<std::mutex> lock(executor->mutex);
        while( executor->tasks.empty() && executor->state == Executor::State::kRun )
            executor->empty_condition.wait(lock);

        if( executor->tasks.empty() || executor->state == Executor::State::kStopped )
            break;

        if( executor->state != Executor::State::kStopped ) {
            std::function<void()> exec = executor->tasks.front();
            executor->tasks.pop_front();
            lock.unlock();
            exec();
        }
    }
}

Executor::Executor(std::string name, int size) {
    state = State::kRun;
    for(int i = 0; i < size; ++i)
        threads.emplace_back(perform, this);
}

Executor::~Executor() {}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> lock(this->mutex);
    state = State::kStopping;
    if( await ) {
        state = State::kStopped;
    }
    empty_condition.notify_all();
    lock.unlock();
    for( auto& t : threads )
        t.join();
}

} // namespace Afina