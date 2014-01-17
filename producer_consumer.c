/*
 * Prelab pthreads code fix
 * Tim Martin
 * 2014-1-16
 *
 *
 * BUGS:
 * ==================
 * 1. Must initialize g_num_prod_lock mutex. L69
 * 2. Remove attempted join of detached producer thread. L108
 * 3. Join, don't detach, the producer's consumer thread. This ensures proper
 *    clean up of heap-allocated memory, among other things (See comment). L170
 * 4. To report the printed letter count to the join caller, consumers must
 *    return a pointer to a non-locally stored count address. Update count
 *    usage respectively. L241
 * 5. Ensure proper re-locking of g_num_prod_lock and queue_p->lock mutexes.
 *    Unlocking an already-locked mutex is undefined. L292
 *
 * 6. Mini bug: Decrement g_num_prod with mutex. L214
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>


/* Queue Structures */

typedef struct queue_node_s {
	struct queue_node_s *next;
	struct queue_node_s *prev;
	char c;
} queue_node_t;

typedef struct {
	struct queue_node_s *front;
	struct queue_node_s *back;
	pthread_mutex_t lock;
} queue_t;


/* Thread Function Prototypes */
void *producer_routine(void *arg);
void *consumer_routine(void *arg);


/* Global Data */
long g_num_prod; /* number of producer threads */
pthread_mutex_t g_num_prod_lock;

pthread_t consumer_thread_2;

/* Main - entry point */
int main(int argc, char **argv) {
	queue_t queue;
	pthread_t producer_thread, consumer_thread;
	void *thread_return = NULL;
	int result = 0;

	/* Initialization */

	printf("Main thread started with thread id %lu\n", pthread_self());

	memset(&queue, 0, sizeof(queue));
	pthread_mutex_init(&queue.lock, NULL);
	/*
	 * ===========BUG===========
	 * Not necessarily a problem on THIS line, but SOMEWHERE, we need to
	 * initialize the g_num_prod_lock mutex. It wasn't being initialized
	 * before.
	 */
	pthread_mutex_init(&g_num_prod_lock, NULL);

	g_num_prod = 1; /* there will be 1 producer thread */

	/* Create producer and consumer threads */

	result = pthread_create(&producer_thread, NULL, producer_routine,
				&queue);
	if (0 != result) {
		fprintf(stderr, "Failed to create producer thread: %s\n",
			strerror(result));
		exit(1);
	}

	printf("Producer thread started with thread id %lu\n",
	       producer_thread);

	/* result = pthread_detach(producer_thread); */
	/* if (0 != result) */
	/* fprintf(stderr, "Failed to detach producer thread: %s\n",
	 * strerror(result)); */

	result = pthread_create(&consumer_thread, NULL, consumer_routine,
				&queue);
	if (0 != result) {
		fprintf(stderr, "Failed to create consumer thread: %s\n",
			strerror(result));
		exit(1);
	}

	/* Join threads, handle return values where appropriate */


	/*
	 * ===========BUG===========
	 * You can't try to join a detached thread. Removing this chunk.
	 *
	 * result = pthread_join(producer_thread, NULL);
	 *  if (0 != result) {
	 *    fprintf(stderr, "Failed to join producer thread: %s\n",
	 *    strerror(result));
	 *    pthread_exit(NULL);
	 * }
	 */

	result = pthread_join(producer_thread, NULL);
	if (0 != result) {
		fprintf(stderr, "Failed to join consumer thread: %s\n",
			strerror(result));
		pthread_exit(NULL);
	}

	result = pthread_join(consumer_thread, &thread_return);
	if (0 != result) {
		fprintf(stderr, "Failed to join consumer thread: %s\n",
			strerror(result));
		pthread_exit(NULL);
	}
	printf("\nPrinted %lu characters.\n", *(long*)thread_return);
	free(thread_return);

	/* See explanation in producer_routine as to why we're doing this
	 * here.
	 */
	result = pthread_join(consumer_thread_2, &thread_return);
	if (0 != result) {
		fprintf(stderr, "Failed to join consumer thread (2): %s\n",
			strerror(result));
		pthread_exit(NULL);
	}
	printf("\nPrinted %lu characters. (consumer_thread_2)\n",
	       *(long*)thread_return);
	free(thread_return);

	pthread_mutex_destroy(&queue.lock);
	pthread_mutex_destroy(&g_num_prod_lock);
	return 0;
}


/* Function Definitions */

/* producer_routine - thread that adds the letters 'a'-'z' to the queue */
void *producer_routine(void *arg) {
	queue_t *queue_p = arg;
	queue_node_t *new_node_p = NULL;
	int result = 0;
	char c;

	result = pthread_create(&consumer_thread_2, NULL, consumer_routine,
				queue_p);
	if (0 != result) {
		fprintf(stderr, "Failed to create consumer thread: %s\n",
			strerror(result));
		exit(1);
	}
	/* ===========BUG===========
	 * Without a join, there's no guarantee it can even run in this
	 * program, no gaurantee it will print letters it pops off the queue,
	 * no one to pass the count to (for reporting), and it might allocate
	 * memory that will never be freed.
	 *
	 * So, we take out the detach here and then join where the other
	 * consumer gets joined, so we can see the number of characters it
	 * printed. This requires a global consumer_thread_2.
	 *
	 * result = pthread_detach(consumer_thread2);
	 * if (0 != result)
	 * fprintf(stderr, "Failed to detach consumer thread: %s\n",
	 * strerror(result));
	 */

	for (c = 'a'; c <= 'z'; ++c) {

		/* Create a new node with the prev letter */
		new_node_p = malloc(sizeof(queue_node_t));
		new_node_p->c = c;
		new_node_p->next = NULL;

		/* Add the node to the queue */
		pthread_mutex_lock(&queue_p->lock);
		if (queue_p->back == NULL) {
			assert(queue_p->front == NULL);
			new_node_p->prev = NULL;

			queue_p->front = new_node_p;
			queue_p->back = new_node_p;
		}
		else {
			assert(queue_p->front != NULL);
			new_node_p->prev = queue_p->back;
			queue_p->back->next = new_node_p;
			queue_p->back = new_node_p;
		}
		pthread_mutex_unlock(&queue_p->lock);

		sched_yield();
	}

	/* Decrement the number of producer threads running, then return */
	/* ===========BUG===========
	 * We need to lock the g_num_prod_lock mutex to prevent race conditions
	 * on this g_num_prod. (Decrementing is not an atomic operation).
	 *
	 *        producer       |     consumer
	 *  =====================|=========================
	 *  read g_num_prod (1)  |
	 *                       |  read g_num_prod (1)
	 *  calc g_num_prod - 1  |
	 *  write back g_num_prod|
	 *                       |  go through another unneeded queue check
	 *                          iteration
	 *
	 *
	 */
	pthread_mutex_lock(&g_num_prod_lock);
	--g_num_prod;
	pthread_mutex_unlock(&g_num_prod_lock);

	return (void*) 0;
}


/* consumer_routine - thread that prints characters off the queue */
void *consumer_routine(void *arg) {
	queue_t *queue_p = arg;
	queue_node_t *prev_node_p = NULL;
	/* ===========BUG===========
	 * if we want to pass count to pthread_join, it's got to be a pointer
	 * to heap memory, not stack/local memory.
	 *
	 * long count = 0; [> number of nodes this thread printed <]
	 */
	long * count = malloc(sizeof(long)); /* number of nodes this thread
						printed */
	*count = 0;

	printf("Consumer thread started with thread id %lu\n",
	       pthread_self());

	/* terminate the loop only when there are no more items in the queue
	 * AND the producer threads are all done */

	pthread_mutex_lock(&queue_p->lock);
	pthread_mutex_lock(&g_num_prod_lock);
	while(queue_p->front != NULL || g_num_prod > 0) {
		pthread_mutex_unlock(&g_num_prod_lock);

		if (queue_p->front != NULL) {

			/* Remove the prev item from the queue */
			prev_node_p = queue_p->front;

			if (queue_p->front->next == NULL)
				queue_p->back = NULL;
			else
				queue_p->front->next->prev = NULL;

			queue_p->front = queue_p->front->next;
			pthread_mutex_unlock(&queue_p->lock);

			/* Print the character, and increment the character
			 * count */
			printf("%c", prev_node_p->c);
			free(prev_node_p);
			/* ===========BUG===========
			 * We're dealing with a pointer now, so dereference,
			 * then increment.
			 *
			 * ++count;
			 */
			(*count)++;
			sched_yield();
		}
		else { /* Queue is empty, so let some other thread run */
			pthread_mutex_unlock(&queue_p->lock);
			sched_yield();
		}
		/* ===========BUG===========
		 * The conditional of this while loop checks the global
		 * variable g_num_prod.  Before we enter the loop, we lock the
		 * mutex for g_num_prod. After the condition is checked we
		 * unlock. If the condition passes again, we unlock again too,
		 * resulting in unlocking an already-unlocked mutex.
		 *
		 * This behavior is undefined, and we must lock again here at
		 * the end of the loop and also prevents race conditions in
		 * reading g_num_prod.
		 *
		 * This all also applies to queue_p->lock.
		 */
		pthread_mutex_lock(&g_num_prod_lock);
		pthread_mutex_lock(&queue_p->lock);
	}
	pthread_mutex_unlock(&g_num_prod_lock);
	pthread_mutex_unlock(&queue_p->lock);

	/* ===========BUG===========
	 * don't cast to pointer, it IS a pointer.
	 *
	 * return (void*) count;
	 */
	return count;
}
