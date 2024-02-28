#include "opt-synchprobs.h"
#include "counter.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>


/*
 * Declare the counter variable that all threads increment or decrement
 * via the interface provided here.
 *
 * Declaring it "volatile" instructs the compiler to always (re)read the
 * variable from memory and not to optimise by keeping the value in a process 
 * register and avoid memory references.
 *
 * NOTE: The volatile declaration is actually not needed for the provided code 
 * as the variable is only loaded once in each function.
 */

static volatile int the_counter;
struct lock *mutex;

void counter_increment(void)
{
        lock_acquire(mutex);
        the_counter = the_counter + 1;
        lock_release(mutex);
}

void counter_decrement(void)
{
        lock_acquire(mutex);
        the_counter = the_counter - 1;
        lock_release(mutex);
}

int counter_initialise(int val)
{
        the_counter = val;
        mutex = lock_create("mutex");
        if (mutex == NULL) return ENOMEM; // allocation failure 
        return 0;
}

int counter_read_and_destroy(void)
{
        lock_destroy(mutex);
        return the_counter;
}
