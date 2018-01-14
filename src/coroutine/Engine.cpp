#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
//    std::cout << "coroutine debug: " << __PRETTY_FUNCTION__ << std::endl;
    // set beginning and end of routine's stack
    char stack_end;
    ctx.Low = StackBottom;
    ctx.Hight = &stack_end;

    // copy routine's stack in context's buffer
    std::get<1>(ctx.Stack) = ctx.Low - ctx.Hight;  // because stack grows downwards
    if( std::get<0>(ctx.Stack) != nullptr )
        delete[] std::get<0>(ctx.Stack);
    std::get<0>(ctx.Stack) = new char[std::get<1>(ctx.Stack)]; // allocate memory for copy of stack
    // create full copy of stack
    // note that stack grows downwards, so we take these addresses as start points for copying
    memcpy(std::get<0>(ctx.Stack), (void*) ctx.Hight, std::get<1>(ctx.Stack));
}

void Engine::Restore(context &ctx) {
//    std::cout << "coroutine debug: " << __PRETTY_FUNCTION__ << std::endl;
    // write stack copy from context into actual stack
    char stack_end;
    // we may have to enlarge stack so that we could paste our copy
    if( (char*)&stack_end > StackBottom - std::get<1>(ctx.Stack) ) {
        int64_t placeholder = 0;
        Restore(ctx);
    }

    memcpy(StackBottom - std::get<1>(ctx.Stack), std::get<0>(ctx.Stack), std::get<1>(ctx.Stack));
    longjmp(ctx.Environment, 1);  // val is what is returned by setjmp
}

void Engine::yield() {
//    std::cout << "coroutine debug: " << __PRETTY_FUNCTION__ << std::endl;
    if( alive != nullptr ) {
        // take the first task from alive list
        context *task = alive;
        if( alive->next != nullptr )
            alive->next->prev = nullptr;
        alive = alive->next;
        sched((void*)task);
    }
}

void Engine::sched(void *routine_) {
//    std::cout << "coroutine debug: " << __PRETTY_FUNCTION__ << std::endl;
    context *ctx = (context*)routine_;

    if( cur_routine != nullptr ) {
        Store(*cur_routine);
        if( setjmp(cur_routine->Environment) > 0 )
            return;
    }
    cur_routine = ctx;
    Restore(*ctx);
}

} // namespace Coroutine
} // namespace Afina
