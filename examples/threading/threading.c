#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    thread_func_args->thread_complete_success  = false;

    if (usleep(thread_func_args->wait_to_obtain_ms * 1000) != 0) {
        ERROR_LOG("Waiting for wait_to_obtain_ms which is %d ms failed\n", thread_func_args->wait_to_obtain_ms);
        return thread_func_args;
    }

    if (pthread_mutex_lock(thread_func_args->mutex) != 0) {
        ERROR_LOG("Obtaining lock failed\n");
        return thread_func_args;
    }
    
    if (usleep(thread_func_args->wait_to_release_ms * 1000) != 0) {
        ERROR_LOG("Waiting for wait_to_release_ms which is %d ms failed\n", thread_func_args->wait_to_release_ms);
        pthread_mutex_unlock(thread_func_args->mutex);
        return thread_func_args;
    }

    if (pthread_mutex_unlock(thread_func_args->mutex) != 0) {
        ERROR_LOG("Releasing lock failed\n");
        return thread_func_args;
    }
    
    thread_func_args->thread_complete_success  = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    int rc;
    
    struct thread_data *thread_data_td = (struct thread_data *) malloc(sizeof(struct thread_data));
    if ( thread_data_td == NULL)  {
        ERROR_LOG("Allocating memory for thread data failed\n");
        return false;
    }
    thread_data_td->wait_to_obtain_ms       = wait_to_obtain_ms;
    thread_data_td->wait_to_release_ms      = wait_to_release_ms;
    thread_data_td->mutex                   = mutex;
    thread_data_td->thread_complete_success = false;
    
    rc = pthread_create(  thread,
                                NULL, // Use default attributes
                                threadfunc,
                                thread_data_td);
    if( rc != 0)  {
        ERROR_LOG("pthread_create failed with error %d creating thread\n",rc);
        return false;
    }
    if ( thread == NULL ) {
        ERROR_LOG("pthread_create returned nullpointer\n");
        return false;        
    }
    /* Joining the thread and freeing thread_data_td is done by the caller */
    return true;
}

