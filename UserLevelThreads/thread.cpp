
#include "thread.h"
#ifdef __x86_64__
/* code for 64 bit Intel arch */


#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif


thread::thread(int id, void (*func)(void)): _id(id), _func(func), _quantum_counter(0), _state(READY),
                                            normal_lock(false), mutex_lock(false)
{
    _stack = new char[STACK_SIZE];
    _sp = (address_t)_stack + STACK_SIZE - sizeof(address_t);
    _pc = (address_t)_func;
    sigsetjmp(_env, 1);
    (_env->__jmpbuf)[JB_SP] = translate_address(_sp);
    (_env->__jmpbuf)[JB_PC] = translate_address(_pc);
    sigemptyset(&_env->__saved_mask);
}
