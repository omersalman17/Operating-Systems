
#include "uthreads.h"
#include "thread.h"
#include <iostream>
#include <list>
#include <signal.h>


#define BLOCK_SIGVTALRM sigprocmask(SIG_BLOCK, &sig_set, NULL)
#define UNBLOCK_SIGVTALRM sigprocmask(SIG_UNBLOCK, &sig_set, NULL)


thread* threads[MAX_THREAD_NUM] = {nullptr};
std::list<int> ready_threads_ids;
std::list<int> blocked_threads_ids;
int running_thread_id;
int mutex_thread_id;

struct sigaction sa;
struct itimerval timer;
sigset_t sig_set;
static int threads_quantum_usecs;
int total_quantum;


void delete_all()
{
    ready_threads_ids.clear();
    blocked_threads_ids.clear();
    for (thread* thread_p: threads)
    {
        if (thread_p != nullptr)
        {
            delete thread_p;
            thread_p = nullptr;
        }
    }
}

void switch_threads(bool terminate_current_thread=false)
{
    BLOCK_SIGVTALRM;

    int ret_val = sigsetjmp(*threads[running_thread_id]->get_env_buf(),1);

    if (ret_val == 1) {
        return;
    }

    if (terminate_current_thread)
    {
        delete threads[running_thread_id];
        threads[running_thread_id] = nullptr;
    }
    else if (threads[running_thread_id]->get_state() == RUNNING)
    {
        threads[running_thread_id]->set_state(READY);
        ready_threads_ids.push_back(running_thread_id);
    }
    else
    {
        if (!threads[running_thread_id]->mutex_lock)
        {
            threads[running_thread_id]->normal_lock = true;
        }
        threads[running_thread_id]->set_state(BLOCKED);
        blocked_threads_ids.push_back(running_thread_id);
    }

    running_thread_id  = ready_threads_ids.front();
    ready_threads_ids.remove(running_thread_id);

    timer.it_value.tv_sec = threads_quantum_usecs / SECOND;		// first time interval, seconds part
    timer.it_value.tv_usec = threads_quantum_usecs;		// first time interval, microseconds part

    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        delete_all();
        std::cerr << "system error: setitimer error.\n";
        exit(1);
    }

    threads[running_thread_id]->set_state(RUNNING);
    threads[running_thread_id]->increase_quantum_counter();

    total_quantum++;
    if (threads[running_thread_id]->mutex_lock)
    {
        threads[running_thread_id]->mutex_lock = false;
        uthread_mutex_lock();
    }
    UNBLOCK_SIGVTALRM;
    siglongjmp(*threads[running_thread_id]->get_env_buf(), 1);
}

void sigation_switch_threads(int s)
{
    switch_threads();
}

int get_threads_index()
{
    for (int i=0; i<MAX_THREAD_NUM; i++)
    {
        if (threads[i] == nullptr) {return i;}
    }
    return -1;
}

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs)
{
    if (quantum_usecs <= 0)
    {
        std::cerr << "thread library error: non-positive quantum_usecs\n";
        return -1;
    }

    threads_quantum_usecs = quantum_usecs;

    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGVTALRM);

    sa.sa_handler = &sigation_switch_threads;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        std::cerr << "system error: sigaction failed.\n";
        exit(1);
    }

    thread* main_thread = new thread(0, nullptr);
    running_thread_id = 0;
    mutex_thread_id = -1;
    threads[running_thread_id] = main_thread;
    main_thread->set_state(RUNNING);
    total_quantum = 1;
    threads[running_thread_id]->increase_quantum_counter();

    timer.it_value.tv_sec = quantum_usecs / SECOND;		// first time interval, seconds part
    timer.it_value.tv_usec = quantum_usecs;		// first time interval, microseconds part


    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        delete_all();
        std::cerr << "system error: setitimer error.\n";
        exit(1);
    }
    // end of time handling
    return 0;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void))
{
    BLOCK_SIGVTALRM;
    // get ready_index
    int thread_id = get_threads_index();
    if (thread_id == -1)
    {
        std::cerr << "thread library error: number of concurrent threads exceeded the limit \n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    auto* new_thread = new thread(thread_id, f);
    threads[thread_id] = new_thread;
    ready_threads_ids.push_back(thread_id);

    UNBLOCK_SIGVTALRM;
    return thread_id;
}


/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    BLOCK_SIGVTALRM;

    if (tid == 0)
    {
        delete_all();
        exit(0);
    }
    if ((tid < 0) | (tid >= MAX_THREAD_NUM))
    {
        std::cerr << "thread library error: tid not in range (0 to MAX_THREAD_NUM-1).\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: thread with given tid doesn't exist.\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    if (running_thread_id == tid)
    {
        if (mutex_thread_id == tid)
        {
            uthread_mutex_unlock();
        }
        // switch threads
        switch_threads(true);
    }
    else
    {
        ready_threads_ids.remove(tid);
        blocked_threads_ids.remove(tid);
        delete threads[tid];
        threads[tid] = nullptr;
    }
    UNBLOCK_SIGVTALRM;
    return 0;
}


/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    BLOCK_SIGVTALRM;

    if ((tid < 0) | (tid >= MAX_THREAD_NUM))
    {
        std::cerr << "thread library error: tid not in range (0 to MAX_THREAD_NUM-1).\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: thread with given tid doesn't exist.\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    if (tid == 0)
    {
        std::cerr << "thread library error: cannot block thread 0.\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }

    if (running_thread_id == tid)
    {
        // switch threads
        threads[tid]->set_state(BLOCKED);
        switch_threads();
    }
    else if (threads[tid]->get_state() == READY)
    {
        threads[tid]->set_state(BLOCKED);
        ready_threads_ids.remove(tid);
        blocked_threads_ids.push_back(tid);
    }
    threads[tid]->normal_lock = true;
    UNBLOCK_SIGVTALRM;
    return 0;
}


/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    BLOCK_SIGVTALRM;

    if ((tid < 0) | (tid >= MAX_THREAD_NUM))
    {
        std::cerr << "thread library error: tid not in range (0 to MAX_THREAD_NUM-1).\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: thread with given tid doesn't exist.\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }

    if (threads[tid]->get_state() == BLOCKED)
    {
        threads[tid]->set_state(READY);
        threads[tid]->normal_lock = false;
        blocked_threads_ids.remove(tid);
        ready_threads_ids.push_back(tid);
    }

    UNBLOCK_SIGVTALRM;
    return 0;
}


/*
 * Description: This function tries to acquire a mutex.
 * If the mutex is unlocked, it locks it and returns.
 * If the mutex is already locked by different thread, the thread moves to BLOCK state.
 * In the future when this thread will be back to RUNNING state,
 * it will try again to acquire the mutex.
 * If the mutex is already locked by this thread, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_lock()
{
    BLOCK_SIGVTALRM;

    if (mutex_thread_id == -1)
    {
        mutex_thread_id = running_thread_id;
    }
    else if (mutex_thread_id == running_thread_id )
    {
        std::cerr << "thread library error: cannot lock the mutex twice.\n";
        UNBLOCK_SIGVTALRM;
        return  -1;
    }
    else {
        threads[running_thread_id]->set_state(BLOCKED);
        threads[running_thread_id]->mutex_lock = true;
        switch_threads();
    }
    UNBLOCK_SIGVTALRM;
    return 0;
}


/*
 * Description: This function releases a mutex.
 * If there are blocked threads waiting for this mutex,
 * one of them (no matter which one) moves to READY state.
 * If the mutex is already unlocked, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_unlock()
{
    BLOCK_SIGVTALRM;
    if (running_thread_id != mutex_thread_id){
        std::cerr << "thread library error: Only the thread that locked the mutex can unlock it.\n";
        UNBLOCK_SIGVTALRM;
        return  -1;
    }
    auto tid = -1;
    mutex_thread_id = -1;
    for (int id: blocked_threads_ids)
    {
        if (threads[id]->mutex_lock && !threads[id]->normal_lock)
        {
            tid = id;
            break;
        }
    }
    if (tid != -1) {uthread_resume(tid);}

    UNBLOCK_SIGVTALRM;
    return 0;
}


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid()
{
    return running_thread_id;
}


/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    return total_quantum;
}


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    BLOCK_SIGVTALRM;
    if ((tid < 0) | (tid >= MAX_THREAD_NUM))
    {
        std::cerr << "thread library error: tid not in range (0 to MAX_THREAD_NUM-1).\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    if (threads[tid] == nullptr)
    {
        std::cerr << "thread library error: thread with given tid doesn't exist.\n";
        UNBLOCK_SIGVTALRM;
        return -1;
    }
    UNBLOCK_SIGVTALRM;
    return threads[tid]->get_quantum_counter();
}
