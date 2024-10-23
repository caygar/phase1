#include <phase1.h>
#include <usloss.h>
#include <string.h>
#include <stdlib.h>
#include "phase2.h"



// Mailbox and mail slot data structures
typedef struct MailSlot {
    int id;
    char message[MAX_MESSAGE];
    int messageSize;
    struct MailSlot *next;
} MailSlot;

typedef struct ProcessQueue {
    int pid;
    struct ProcessQueue *next;
} ProcessQueue;

typedef struct Mailbox {
    int id;
    int numSlots;
    int slotSize;
    int usedSlots;
    MailSlot *slots;
    int isReleased;
    ProcessQueue *producerQueue;
    ProcessQueue *consumerQueue;
} Mailbox;

static Mailbox mailboxes[MAXMBOX];
static MailSlot mailSlots[MAXSLOTS];
static int nextAvailableSlot = 0;

void enqueueProcess(ProcessQueue **queue, int pid) {
    ProcessQueue *newProcess = (ProcessQueue *)malloc(sizeof(ProcessQueue));
    newProcess->pid = pid;
    newProcess->next = NULL;

    if (*queue == NULL) {
        *queue = newProcess;
    } else {
        ProcessQueue *current = *queue;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = newProcess;
    }
}

int dequeueProcess(ProcessQueue **queue) {
    if (*queue == NULL) {
        return -1;
    }

    ProcessQueue *dequeued = *queue;
    int pid = dequeued->pid;
    *queue = (*queue)->next;
    free(dequeued);
    return pid;
}

void phase2_init(void) {
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].id = -1;
        mailboxes[i].usedSlots = 0;
        mailboxes[i].isReleased = 0;
        mailboxes[i].slots = NULL;
        mailboxes[i].producerQueue = NULL;
        mailboxes[i].consumerQueue = NULL;
    }
    for (int i = 0; i < MAXSLOTS; i++) {
        mailSlots[i].id = -1;
        mailSlots[i].next = NULL;
    }
}

int MboxCreate(int numSlots, int slotSize) {
    if (numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE) {
        return -1;
    }

    for (int i = 7; i < MAXMBOX; i++) {
        if (mailboxes[i].id == -1) {
            mailboxes[i].id = i;
            mailboxes[i].numSlots = numSlots;
            mailboxes[i].slotSize = slotSize;
            return i;
        }
    }
    return -1;  // No available mailbox
}

int MboxRelease(int mailboxID) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1 || mailboxes[mailboxID].isReleased) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    mbox->isReleased = 1;
    mbox->id = -1;
    mbox->usedSlots = 0;
    // Clean up mail slots
    while (mbox->slots != NULL) {
        MailSlot *slot = mbox->slots;
        mbox->slots = slot->next;
        slot->id = -1;
        slot->next = NULL;
    }
    // Unblock all processes in the producer queue
    while (mbox->producerQueue != NULL) {
        int pid = dequeueProcess(&mbox->producerQueue);
        if (pid != -1) {
            unblockProc(pid);
        }
    }
    // Unblock all processes in the consumer queue
    while (mbox->consumerQueue != NULL) {
        int pid = dequeueProcess(&mbox->consumerQueue);
        if (pid != -1) {
            unblockProc(pid);
        }
    }

    return 0;
}

int MboxSend(int mailboxID, void *message, int messageSize) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (messageSize > mbox->slotSize) {
        return -1; // Message too large
    }

    if (mbox->usedSlots >= mbox->numSlots) {
        if (mbox->isReleased) {
            return -1;
        }
        enqueueProcess(&mbox->producerQueue, getpid());
        blockMe();
        if (mbox->isReleased) {
            return -1;
        }
    }

    // Allocate a mail slot from the global pool
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].id == -1) {
            mailSlots[i].id = i;
            memcpy(mailSlots[i].message, message, messageSize);
            mailSlots[i].messageSize = messageSize;
            mailSlots[i].next = NULL;
            if (mbox->slots == NULL) {
                mbox->slots = &mailSlots[i];
            } else {
                MailSlot *current = mbox->slots;
                while (current->next != NULL) {
                    current = current->next;
                }
                current->next = &mailSlots[i];
            }
            mbox->usedSlots++;

            int consumerPid = dequeueProcess(&mbox->consumerQueue);
            if (consumerPid != -1) {
                unblockProc(consumerPid);
            }
            return 0;
        }
    }

    return -2; // No global slots available
}

int MboxRecv(int mailboxID, void *message, int maxMessageSize) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->slots == NULL) {
        if (mbox->isReleased) {
            return -1;
        }
        enqueueProcess(&mbox->consumerQueue, getpid());
        blockMe();
        if (mbox->isReleased) {
            return -1;
        }
    }

    MailSlot *slot = mbox->slots;
    if (slot->messageSize > maxMessageSize) {
        return -1; // Buffer too small
    }

    memcpy(message, slot->message, slot->messageSize);
    mbox->slots = slot->next;
    slot->id = -1;
    mbox->usedSlots--;

    int producerPid = dequeueProcess(&mbox->producerQueue);
    if (producerPid != -1) {
        unblockProc(producerPid);
    }

    // Return the size of the received message
    return slot->messageSize;
}


int MboxCondSend(int mailboxID, void *message, int messageSize) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->usedSlots >= mbox->numSlots) {
        return -2;
    }

    return MboxSend(mailboxID, message, messageSize);
}

int MboxCondRecv(int mailboxID, void *message, int maxMessageSize) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->slots == NULL) {
        return -2;
    }

    return MboxRecv(mailboxID, message, maxMessageSize);
}

void waitDevice(int type, int unit, int *status) {
    // Implement wait for device interrupt (clock, disk, terminal)
    // Placeholder for actual implementation, requires integration with interrupt handlers
}

void phase2_start_service_processes(void) {
    // Called after init, use spork() if any service processes are needed
}
