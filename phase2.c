#include <phase1.h>
#include <usloss.h>
#include <string.h>
#include <stdlib.h>
#include "phase2.h"

#define CLOCK_MBOX 0
#define DISK0_MBOX 1
#define DISK1_MBOX 2
#define TERM0_MBOX 3
#define TERM1_MBOX 4
#define TERM2_MBOX 5
#define TERM3_MBOX 6

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
static int lastTimeSent = 0; // Last time we sent a message on clock

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

    MboxCreate(1, sizeof(int)); // Clock
    MboxCreate(1, sizeof(int)); // Disk 0
    MboxCreate(1, sizeof(int)); // Disk 1
    MboxCreate(1, sizeof(int)); // Terminal 0
    MboxCreate(1, sizeof(int)); // Terminal 1
    MboxCreate(1, sizeof(int)); // Terminal 2
    MboxCreate(1, sizeof(int)); // Terminal 3

    USLOSS_IntVec[USLOSS_CLOCK_INT] = clockHandler;
    USLOSS_IntVec[USLOSS_DISK_INT] = diskHandler;
    USLOSS_IntVec[USLOSS_TERM_INT] = termHandler;

    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
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
    return -1;
}

int MboxRelease(int mailboxID) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1 || mailboxes[mailboxID].isReleased) {
        return -1;
    }
    Mailbox *mbox = &mailboxes[mailboxID];
    mbox->isReleased = 1;
    mbox->id = -1;
    mbox->usedSlots = 0;
    while (mbox->slots != NULL) {
        MailSlot *slot = mbox->slots;
        mbox->slots = slot->next;
        slot->id = -1;
        slot->next = NULL;
    }
    while (mbox->producerQueue != NULL) {
        int pid = dequeueProcess(&mbox->producerQueue);
        if (pid != -1) {
            unblockProc(pid);
        }
    }
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
        return -1;
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
    return -2;
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
        return -1;
    }
    memcpy(message, slot->message, slot->messageSize);
    mbox->slots = slot->next;
    slot->id = -1;
    mbox->usedSlots--;
    int producerPid = dequeueProcess(&mbox->producerQueue);
    if (producerPid != -1) {
        unblockProc(producerPid);
    }
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

void clockHandler(int type, void *arg) {
    int currentTime;
    int status;
    USLOSS_DeviceInput(USLOSS_CLOCK_DEV, 0, &currentTime);
    if (currentTime - lastTimeSent >= 100) {
        MboxCondSend(CLOCK_MBOX, &status, sizeof(status));
        lastTimeSent = currentTime;
    }
    dispatcher();
}

void diskHandler(int type, void *arg) {
    int status;
    int unitNo = (int)(long)arg;
    USLOSS_DeviceInput(USLOSS_DISK_DEV, unitNo, &status);
    if (unitNo == 0) {
        MboxCondSend(DISK0_MBOX, &status, sizeof(status));
    } else if (unitNo == 1) {
        MboxCondSend(DISK1_MBOX, &status, sizeof(status));
    }
    dispatcher();
}

void termHandler(int type, void *arg) {
    int status;
    int unitNo = (int)(long)arg;
    USLOSS_DeviceInput(USLOSS_TERM_DEV, unitNo, &status);
    switch (unitNo) {
        case 0:
            MboxCondSend(TERM0_MBOX, &status, sizeof(status));
            break;
        case 1:
            MboxCondSend(TERM1_MBOX, &status, sizeof(status));
            break;
        case 2:
            MboxCondSend(TERM2_MBOX, &status, sizeof(status));
            break;
        case 3:
            MboxCondSend(TERM3_MBOX, &status, sizeof(status));
            break;
    }
    dispatcher();
}

void waitDevice(int type, int unit, int *status) {
    int mboxID;
    switch (type) {
        case USLOSS_CLOCK_DEV:
            mboxID = CLOCK_MBOX;
            break;
        case USLOSS_DISK_DEV:
            mboxID = (unit == 0) ? DISK0_MBOX : DISK1_MBOX;
            break;
        case USLOSS_TERM_DEV:
            switch (unit) {
                case 0: mboxID = TERM0_MBOX; break;
                case 1: mboxID = TERM1_MBOX; break;
                case 2: mboxID = TERM2_MBOX; break;
                case 3: mboxID = TERM3_MBOX; break;
                default: USLOSS_Halt(1);
            }
            break;
        default:
            USLOSS_Halt(1);
    }
    MboxRecv(mboxID, status, sizeof(int));
}

void nullsys(USLOSS_Sysargs *sysargs) {
    USLOSS_Console("Invalid system call number: %d\n", sysargs->number);
    USLOSS_Halt(1);
}
