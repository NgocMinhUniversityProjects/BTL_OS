#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t * q) {
        if (q == NULL) return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t * q, struct pcb_t * proc) {
        /* TODO: put a new process to queue [q] */
        if (q == NULL) return;
        if (q->size >= MAX_QUEUE_SIZE) return;
        if (q->front == q->rear && q->size == 0) { // queue is empty
                q->proc[q->front] = proc;
                q->size++;
        }
        else { // queue is not empty
                q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
                q->proc[q->rear] = proc;
                q->size++;
        }
        
}

struct pcb_t * dequeue(struct queue_t * q) {
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (q == NULL) return NULL;
        if (q->size == 0) return NULL;
        struct pcb_t * proc = q->proc[q->front];
        if (q->size > 1) {
                q->front = (q->front + 1) % MAX_QUEUE_SIZE;
        }
        q->size--;
        return proc;
}

