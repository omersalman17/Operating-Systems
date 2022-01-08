
#ifndef EX2_THREAD_H
#define EX2_THREAD_H


#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#define SECOND 1000000
#define STACK_SIZE 4096


typedef unsigned long address_t;

#define READY 0
#define RUNNING 1
#define BLOCKED 2


class thread {
public:
    bool normal_lock;
    bool mutex_lock;

    thread(int id, void (*func)(void));
    ~thread(){delete _stack;}
    int get_quantum_counter(){return _quantum_counter;}
    sigjmp_buf* get_env_buf() {return &_env;}
    void set_state(int state) {_state = state;}
    int get_state() {return _state;}
    void increase_quantum_counter() {_quantum_counter++;}
private:
    int _id;
    void (*_func)(void);
    int _state;
    int _quantum_counter;
    char *_stack;
    sigjmp_buf _env;
    address_t _sp, _pc;
};


#endif //EX2_THREAD_H
