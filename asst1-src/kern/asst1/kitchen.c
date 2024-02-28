#include "opt-synchprobs.h"
#include "kitchen.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <test.h>
#include <thread.h>
#include <synch.h>

struct cv *empty, *full;
struct lock *l;

static int soup_servings = 0;

/*
 * initialise_kitchen: 
 *
 * This function is called during the initialisation phase of the
 * kitchen, i.e.before any threads are created.
 *
 * Initialise any global variables or create any synchronisation
 * primitives here.
 * 
 * The function returns 0 on success or non-zero on failure.
 */

int initialise_kitchen()
{
        empty = cv_create("empty");
        full = cv_create("full");
        l = lock_create("lock");

        // check for successful allocation
        if (empty == NULL || full == NULL || l == NULL) 
                return ENOMEM;

        return 0;
}

/*
 * cleanup_kitchen:
 *
 * This function is called after the dining threads and cook thread
 * have exited the system. You should deallocated any memory allocated
 * by the initialisation phase (e.g. destroy any synchronisation
 * primitives).
 */

void cleanup_kitchen()
{
        cv_destroy(empty);  
        cv_destroy(full);
        lock_destroy(l);
}

/*
 * do_cooking:
 *
 * This function is called repeatedly by the cook thread to provide
 * enough soup to dining customers. It creates soup by calling
 * cook_soup_in_pot().
 *
 * It should wait until the pot is empty before calling
 * cook_soup_in_pot().
 *
 * It should wake any dining threads waiting for more soup.
 */

void do_cooking()
{
        lock_acquire(l);

        // wait until pot is empty
        while (soup_servings > 0) 
            cv_wait(full, l);

        cook_soup_in_pot();
        soup_servings += POTSIZE_IN_SERVES;

        // wake all dining threads waiting on cv empty
        cv_broadcast(empty, l); 

        lock_release(l);
}

/*
 * fill_bowl:
 *
 * This function is called repeatedly by dining threads to obtain soup
 * to satify their hunger. Dining threads fill their bowl by calling
 * get_serving_from_pot().
 *
 * It should wait until there is soup in the pot before calling
 * get_serving_from_pot().
 *
 * get_serving_from_pot() should be called mutually exclusively as
 * only one thread can fill their bowl at a time.
 *
 * fill_bowl should wake the cooking thread if there is no soup left
 * in the pot.
 */

void fill_bowl()
{
        lock_acquire(l);

        // wait for some soup in the pot
        while (soup_servings == 0)
            cv_wait(empty, l);
        
        get_serving_from_pot();
        soup_servings--;

        // wake cooking thread again if pot is empty 
        // (assumes only one cooking thread)
        if (soup_servings == 0) cv_signal(full, l);

        lock_release(l);
}
