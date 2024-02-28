/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include "producerconsumer.h"

/* Declare any variables you need here to keep track of and
   synchronise your bounded buffer. A sample declaration of a buffer is shown
   below. It is an array of pointers to items.
   
   You can change this if you choose another implementation. 
   However, your implementation should accept at least BUFFER_SIZE 
   prior to blocking
*/

#define BUFFLEN (BUFFER_SIZE + 1)

data_item_t * item_buffer[BUFFER_SIZE+1];

struct semaphore *mutex, *full, *empty;

volatile int head, tail;

/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. It should not busy wait! */

data_item_t * consumer_receive(void)
{
        P(full);
        P(mutex);
        data_item_t * item;

        item = item_buffer[tail];
        tail = (tail + 1) % BUFFLEN;

        V(mutex);
        V(empty);

        return item;
}

/* producer_send() is called by a producer to store data in your
   bounded buffer.  It should block on a sync primitive if no space is
   available in your buffer. It should not busy wait!*/

void producer_send(data_item_t *item)
{
        P(empty);
        P(mutex);

        item_buffer[head] = item;
        head = (head + 1) % BUFFLEN;

        V(mutex);
        V(full);
}

/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */

void producerconsumer_startup(void)
{
        head = tail = 0;
        empty = sem_create("empty", BUFFER_SIZE);
        full = sem_create("full", 0);
        mutex = sem_create("mutex", 1);
        if (empty == NULL || full == NULL || mutex == NULL)
            panic("Failed mem allocation | Producer/Consumer semaphore startup");
}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void)
{   
    sem_destroy(empty);
    sem_destroy(full);
    sem_destroy(mutex);
}

