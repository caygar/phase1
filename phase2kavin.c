#include <phase1.h>
#include <usloss.h>
#include <string.h>
#include <stdlib.h>
#include "phase2.h"

#define CLOCK_MB     0
#define TERM_MB_BASE 1        
#define DISK_MB_BASE 5        

static int next_time = 0;     

void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

static void clock_handler(int type, void *arg);
static void disk_handler(int type, void *arg);
static void terminal_handler(int type, void *arg);
static void syscallHandler(int type, void *arg);
static void nullsys(USLOSS_Sysargs *args);

typedef struct Phase2Proc {
    int pid;                    
    int status;                  
    int mboxID;                
    void *msgPtr;               
    int msgSize;                
    int isBlocked;              
    struct Phase2Proc *nextProc; 
} Phase2Proc;

static Phase2Proc P2_ProcTable[MAXPROC];

typedef struct MailSlot {
    int id;
    char message[MAX_MESSAGE];
    int messageSize;
    struct MailSlot *next;
} MailSlot;

typedef struct ProcessQueue {
    Phase2Proc *head;
    Phase2Proc *tail;
} ProcessQueue;

typedef struct Mailbox {
    int id;
    int numSlots;
    int slotSize;
    int usedSlots;
    MailSlot *slots_head;
    int isReleased;
    ProcessQueue producerQueue;
    ProcessQueue consumerQueue;
    MailSlot *slots_tail;
} Mailbox;

static Mailbox mailboxes[MAXMBOX];
static MailSlot mailSlots[MAXSLOTS];

static Phase2Proc *getProc(int pid) {
    return &P2_ProcTable[pid % MAXPROC];
}

static void initProc(Phase2Proc *proc) {
    proc->pid = -1;
    proc->status = -1;
    proc->mboxID = -1;
    proc->msgPtr = NULL;
    proc->msgSize = 0;
    proc->isBlocked = 0;
    proc->nextProc = NULL;
}

static void enqueueProcess(ProcessQueue *queue, Phase2Proc *proc) {
    if (queue->tail == NULL) {
        queue->head = queue->tail = proc;
    } else {
        queue->tail->nextProc = proc;
        queue->tail = proc;
    }
    proc->nextProc = NULL;
}

static Phase2Proc *dequeueProcess(ProcessQueue *queue) {
    if (queue->head == NULL) {
        return NULL;
    }
    
    Phase2Proc *proc = queue->head;
    queue->head = proc->nextProc;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    proc->nextProc = NULL;
    return proc;
}

void phase2_init(void) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: phase2_init called while in user mode\n");
        USLOSS_Halt(1);
    }

    for (int i = 0; i < MAXPROC; i++) {
        initProc(&P2_ProcTable[i]);
    }

    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].id = -1;
        mailboxes[i].usedSlots = 0;
        mailboxes[i].isReleased = 0;
        mailboxes[i].slots_head = NULL;
        mailboxes[i].slots_tail = NULL;
        mailboxes[i].producerQueue.head = NULL;
        mailboxes[i].producerQueue.tail = NULL;
        mailboxes[i].consumerQueue.head = NULL;
        mailboxes[i].consumerQueue.tail = NULL;
    }
    
    for (int i = 0; i < MAXSLOTS; i++) {
        mailSlots[i].id = -1;
        mailSlots[i].next = NULL;
    }

    
    int result = MboxCreate(1, sizeof(int));
    if (result != CLOCK_MB) {
        USLOSS_Console("ERROR: failed to create clock mailbox, got id %d\n", result);
        USLOSS_Halt(1);
    }

    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        result = MboxCreate(1, sizeof(int));
        if (result != TERM_MB_BASE + i) {
            USLOSS_Console("ERROR: failed to create terminal mailbox %d, got id %d\n", i, result);
            USLOSS_Halt(1);
        }
    }

    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        result = MboxCreate(1, sizeof(int));
        if (result != DISK_MB_BASE + i) {
            USLOSS_Console("ERROR: failed to create disk mailbox %d, got id %d\n", i, result);
            USLOSS_Halt(1);
        }
    }

    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
    }
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscallHandler;

    USLOSS_IntVec[USLOSS_CLOCK_INT] = clock_handler;
    USLOSS_IntVec[USLOSS_DISK_INT] = disk_handler;
    USLOSS_IntVec[USLOSS_TERM_INT] = terminal_handler;

    next_time = currentTime() + 100000;  
}

int MboxCreate(int numSlots, int slotSize) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: MboxCreate called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (numSlots < 0 || numSlots > MAXSLOTS || slotSize < 0 || slotSize > MAX_MESSAGE) {
        return -1;
    }

    for (int i = 0; i < MAXMBOX; i++) {
        if (mailboxes[i].id == -1) {
            mailboxes[i].id = i;
            mailboxes[i].numSlots = numSlots;
            mailboxes[i].slotSize = slotSize;
            mailboxes[i].usedSlots = 0;
            mailboxes[i].isReleased = 0;
            mailboxes[i].slots_head = NULL;
            mailboxes[i].slots_tail = NULL;
            mailboxes[i].producerQueue.head = NULL;
            mailboxes[i].producerQueue.tail = NULL;
            mailboxes[i].consumerQueue.head = NULL;
            mailboxes[i].consumerQueue.tail = NULL;
            
            return i;
        }
    }
    return -1;
}

int MboxRelease(int mailboxID) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: MboxRelease called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (mailboxID < 0 || mailboxID >= MAXMBOX || 
        mailboxes[mailboxID].id == -1 || mailboxes[mailboxID].isReleased) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    mbox->isReleased = 1;

    Phase2Proc *proc;
    while ((proc = dequeueProcess(&mbox->producerQueue)) != NULL) {
        proc->status = -3;
        unblockProc(proc->pid);
    }
    while ((proc = dequeueProcess(&mbox->consumerQueue)) != NULL) {
        proc->status = -3;
        unblockProc(proc->pid);
    }

    while (mbox->slots_head != NULL) {
        MailSlot *slot = mbox->slots_head;
        mbox->slots_head = slot->next;
        slot->id = -1;
        slot->next = NULL;
    }
    mbox->slots_tail = NULL;

    mbox->id = -1;
    mbox->usedSlots = 0;
    return 0;
}

int MboxSend(int mailboxID, void *msg, int msgSize) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: MboxSend called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (mailboxID < 0 || mailboxID >= MAXMBOX || 
        mailboxes[mailboxID].id == -1 || msgSize > mailboxes[mailboxID].slotSize) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->isReleased) return -1;

    // Get the process's PCB
    Phase2Proc *proc = getProc(getpid());
    proc->pid = getpid();
    proc->msgPtr = msg;
    proc->msgSize = msgSize;
    proc->mboxID = mailboxID;
    proc->status = -1;

    // Check for waiting consumer first
    Phase2Proc *receiver = dequeueProcess(&mbox->consumerQueue);
    if (receiver != NULL) {
        // Direct message delivery
        if (msgSize <= receiver->msgSize) {
            memcpy(receiver->msgPtr, msg, msgSize);
            receiver->status = msgSize;
            unblockProc(receiver->pid);
            return 0;
        } else {
            // Message too large for receiver, put them back in queue
            enqueueProcess(&mbox->consumerQueue, receiver);
            return -1;
        }
    }

    // No waiting consumer, try to queue the message
    if (mbox->usedSlots >= mbox->numSlots) {
        // Mailbox full, must block
        if (mbox->isReleased) return -1;
        proc->isBlocked = 1;
        enqueueProcess(&mbox->producerQueue, proc);
        blockMe();
        if (mbox->isReleased) return -1;
        return 0;  // Successfully sent after being unblocked
    }

    // Find an empty slot
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].id == -1) {
            // Initialize the slot
            mailSlots[i].id = i;
            memcpy(mailSlots[i].message, msg, msgSize);
            mailSlots[i].messageSize = msgSize;
            mailSlots[i].next = NULL;

            // Add to end of queue
            if (mbox->slots_head == NULL) {
                mbox->slots_head = &mailSlots[i];
                mbox->slots_tail = &mailSlots[i];
            } else {
                mbox->slots_tail->next = &mailSlots[i];
                mbox->slots_tail = &mailSlots[i];
            }
            mbox->usedSlots++;
            return 0;
        }
    }

    return -2;
}

int MboxRecv(int mailboxID, void *msg, int maxSize) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: MboxRecv called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->isReleased) return -1;

    // Get the process's PCB
    Phase2Proc *proc = getProc(getpid());
    proc->pid = getpid();
    proc->msgPtr = msg;
    proc->msgSize = maxSize;
    proc->mboxID = mailboxID;
    proc->status = -1;

    // Check for queued message first
    if (mbox->slots_head != NULL) {
        MailSlot *slot = mbox->slots_head;
        if (slot->messageSize > maxSize) {
            return -1;
        }

        // Copy message and update head/tail pointers
        memcpy(msg, slot->message, slot->messageSize);
        int size = slot->messageSize;
        mbox->slots_head = slot->next;
        if (mbox->slots_head == NULL) {
            mbox->slots_tail = NULL;
        }

        // Free the slot
        slot->id = -1;
        slot->next = NULL;
        mbox->usedSlots--;

        // Wake up a blocked producer if any
        if (mbox->producerQueue.head != NULL) {
            Phase2Proc *sender = dequeueProcess(&mbox->producerQueue);
            // Copy the sender's message to an empty slot
            for (int i = 0; i < MAXSLOTS; i++) {
                if (mailSlots[i].id == -1) {
                    mailSlots[i].id = i;
                    memcpy(mailSlots[i].message, sender->msgPtr, sender->msgSize);
                    mailSlots[i].messageSize = sender->msgSize;
                    mailSlots[i].next = NULL;

                    if (mbox->slots_head == NULL) {
                        mbox->slots_head = &mailSlots[i];
                        mbox->slots_tail = &mailSlots[i];
                    } else {
                        mbox->slots_tail->next = &mailSlots[i];
                        mbox->slots_tail = &mailSlots[i];
                    }
                    mbox->usedSlots++;
                    break;
                }
            }
            sender->isBlocked = 0;
            unblockProc(sender->pid);
        }

        return size;
    }

    // No message available, must block
    if (mbox->isReleased) return -1;
    proc->isBlocked = 1;
    enqueueProcess(&mbox->consumerQueue, proc);
    blockMe();
    
    if (mbox->isReleased) return -1;
    return proc->status;
}

int MboxCondSend(int mailboxID, void *msg, int msgSize) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: MboxCondSend called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (mailboxID < 0 || mailboxID >= MAXMBOX || 
        mailboxes[mailboxID].id == -1 || msgSize > mailboxes[mailboxID].slotSize) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->isReleased) return -1;

    Phase2Proc *receiver = dequeueProcess(&mbox->consumerQueue);
    if (receiver != NULL) {
        // Direct message delivery
        if (msgSize <= receiver->msgSize) {
            memcpy(receiver->msgPtr, msg, msgSize);
            receiver->status = msgSize;
            unblockProc(receiver->pid);
            return 0;
        } else {
            enqueueProcess(&mbox->consumerQueue, receiver);
            return -1;
        }
    }

    // No waiting consumer, check if mailbox is full
    if (mbox->usedSlots >= mbox->numSlots) {
        return -2;
    }

    // Find empty slot
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].id == -1) {
            mailSlots[i].id = i;
            memcpy(mailSlots[i].message, msg, msgSize);
            mailSlots[i].messageSize = msgSize;
            mailSlots[i].next = NULL;

            if (mbox->slots_head == NULL) {
                mbox->slots_head = &mailSlots[i];
                mbox->slots_tail = &mailSlots[i];
            } else {
                mbox->slots_tail->next = &mailSlots[i];
                mbox->slots_tail = &mailSlots[i];
            }
            mbox->usedSlots++;
            return 0;
        }
    }
    return -2;
}

int MboxCondRecv(int mailboxID, void *msg, int maxSize) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: MboxCondRecv called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (mailboxID < 0 || mailboxID >= MAXMBOX || mailboxes[mailboxID].id == -1) {
        return -1;
    }

    Mailbox *mbox = &mailboxes[mailboxID];
    if (mbox->isReleased) return -1;

    // Check for queued message first
    if (mbox->slots_head != NULL) {
        MailSlot *slot = mbox->slots_head;
        if (slot->messageSize > maxSize) {
            return -1;
        }

        memcpy(msg, slot->message, slot->messageSize);
        int size = slot->messageSize;

        mbox->slots_head = slot->next;
        if (mbox->slots_head == NULL) {
            mbox->slots_tail = NULL;
        }

        slot->id = -1;
        slot->next = NULL;
        mbox->usedSlots--;

        // Wake up a blocked producer if any
        Phase2Proc *sender = dequeueProcess(&mbox->producerQueue);
        if (sender != NULL) {
            sender->isBlocked = 0;
            unblockProc(sender->pid);
        }

        return size;
    }

    return -2;  
}

static void clock_handler(int type, void *arg) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: clock_handler called while in user mode\n");
        USLOSS_Halt(1);
    }

    int current = currentTime();
    
    if (current >= next_time) {
        int result = MboxCondSend(CLOCK_MB, &current, sizeof(int));
        
        while (next_time <= current) {
            next_time += 100000;
        }
        
        if (result == -2) {
            // Message couldn't be sent - mailbox was full
        } else if (result != 0) {
            USLOSS_Console("ERROR: clock_handler MboxCondSend failed\n");
            USLOSS_Halt(1);
        }
    }

    dispatcher();
}

static void disk_handler(int type, void *arg) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: disk_handler called while in user mode\n");
        USLOSS_Halt(1);
    }

    int unit = (int)(long)arg;
    
    if (unit < 0 || unit >= USLOSS_DISK_UNITS) {
        USLOSS_Console("ERROR: invalid disk unit %d\n", unit);
        USLOSS_Halt(1);
    }

    int status;
    int rc = USLOSS_DeviceInput(USLOSS_DISK_DEV, unit, &status);
    if (rc != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: disk_handler DeviceInput failed: %d\n", rc);
        USLOSS_Halt(1);
    }

    int mbox_id = DISK_MB_BASE + unit;
    int result = MboxCondSend(mbox_id, &status, sizeof(int));
    
    if (result == -2) {
        // Message couldn't be sent - mailbox was full
    } else if (result != 0) {
        USLOSS_Console("ERROR: disk_handler MboxCondSend failed\n");
        USLOSS_Halt(1);
    }

    dispatcher();
}

static void terminal_handler(int type, void *arg) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: terminal_handler called while in user mode\n");
        USLOSS_Halt(1);
    }

    int unit = (int)(long)arg;
    
    if (unit < 0 || unit >= USLOSS_TERM_UNITS) {
        USLOSS_Console("ERROR: invalid terminal unit %d\n", unit);
        USLOSS_Halt(1);
    }

    int status;
    int rc = USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &status);
    if (rc != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: terminal_handler DeviceInput failed: %d\n", rc);
        USLOSS_Halt(1);
    }

    int mbox_id = TERM_MB_BASE + unit;
    int result = MboxCondSend(mbox_id, &status, sizeof(int));
    
    if (result == -2) {
        // Message couldn't be sent - mailbox was full
    } else if (result != 0) {
        USLOSS_Console("ERROR: terminal_handler MboxCondSend failed\n");
        USLOSS_Halt(1);
    }

    dispatcher();
}

static void syscallHandler(int type, void *arg) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: syscallHandler called while in user mode\n");
        USLOSS_Halt(1);
    }
    
    USLOSS_Sysargs *sysargs = (USLOSS_Sysargs *) arg;
    
    if (sysargs->number < 0 || sysargs->number >= MAXSYSCALLS) {
        USLOSS_Console("syscallHandler(): Invalid syscall number %d\n", 
                      sysargs->number);
        USLOSS_Halt(1);
    }
    
    systemCallVec[sysargs->number](sysargs);
    
    dispatcher();
}

static void nullsys(USLOSS_Sysargs *args) {
    USLOSS_Console("nullsys(): Program called an unimplemented syscall.  syscall no: %d   PSR: 0x%02x\n",
                  args->number, USLOSS_PsrGet());
    USLOSS_Halt(1);
}

void waitDevice(int type, int unit, int *status) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: waitDevice called while in user mode\n");
        USLOSS_Halt(1);
    }

    if (status == NULL) {
        USLOSS_Console("ERROR: waitDevice called with NULL status pointer\n");
        USLOSS_Halt(1);
    }

    int mbox_id;
    switch (type) {
        case USLOSS_CLOCK_DEV:
            if (unit != 0) {
                USLOSS_Console("ERROR: invalid clock unit %d\n", unit);
                USLOSS_Halt(1);
            }
            mbox_id = CLOCK_MB;
            break;

        case USLOSS_DISK_DEV:
            if (unit < 0 || unit >= USLOSS_DISK_UNITS) {
                USLOSS_Console("ERROR: invalid disk unit %d\n", unit);
                USLOSS_Halt(1);
            }
            mbox_id = DISK_MB_BASE + unit;
            break;

        case USLOSS_TERM_DEV:
            if (unit < 0 || unit >= USLOSS_TERM_UNITS) {
                USLOSS_Console("ERROR: invalid terminal unit %d\n", unit);
                USLOSS_Halt(1);
            }
            mbox_id = TERM_MB_BASE + unit;
            break;

        default:
            USLOSS_Console("ERROR: invalid device type %d\n", type);
            USLOSS_Halt(1);
    }

    int result = MboxRecv(mbox_id, status, sizeof(int));
    if (result != sizeof(int)) {
        USLOSS_Console("ERROR: waitDevice MboxRecv failed\n");
        USLOSS_Halt(1);
    }
}

void phase2_start_service_processes(void) {
    // No service processes needed for Phase 2
}
