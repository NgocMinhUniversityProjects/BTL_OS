#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t * q) {
        if (q == NULL) return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t * q, struct pcb_t * proc) {
        /* TODO: put a new process to queue [q] */
        if (q->size >= MAX_QUEUE_SIZE) return;

        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t * dequeue(struct queue_t * q) {
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */

        if (q->size == 0) return NULL;

        int maxPrior = MAX_PRIO;
        int pos = -1;

        // Find the process with the highest priority
        for (int i = 0; i < q->size; i++) {
                if (q->proc[i]->prio < maxPrior) {
                        maxPrior = q->proc[i]->prio;
                        pos = i;
                }
        }
        struct pcb_t * res = q->proc[pos];

        // Shift the remaining processes to fill the gap
        for (int i = pos; i < q->size - 1; i++) {
                q->proc[i] = q->proc[i + 1];
        }
        q->size--;
	return res;
}

