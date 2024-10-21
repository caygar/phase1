#include "phase2.h"
#include "usloss.h"

#define MAXMBOX 50
#define MAXSLOTS 100
#define MAX_MESSAGE 100

typedef struct Mailbox {
    int id;
    int numSlots;
    int slotSize;
    int usedSlots;
    int active;
    int blockedProducers;
    int blockedConsumers;
} Mailbox;

typedef struct MailSlot {
    int mailboxID;
    int size;
    char message[MAX_MESSAGE];
} MailSlot;

typedef struct Process {
    int pid;
    int blocked;
} Process;

static Mailbox mailboxes[MAXMBOX];
static MailSlot mailSlots[MAXSLOTS];
static Process processTable[MAXPROC];

int systemCallVec[MAXSYSCALLS];

void phase2_init() {
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].id = -1;
        mailboxes[i].active = 0;
    }
    for (int i = 0; i < MAXSLOTS; i++) {
        mailSlots[i].mailboxID = -1;
        mailSlots[i].size = 0;
    }
    for (int i = 0; i < MAXPROC; i++) {
        processTable[i].pid = -1;
        processTable[i].blocked = 0;
    }
    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = (int)nullsys;
    }
}

int MboxCreate(int numSlots, int slotSize) {
    if (numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE) {
        return -1;
    }
    int mailboxID = -1;
    for (int i = 0; i < MAXMBOX; i++) {
        if (mailboxes[i].id == -1) {
            mailboxID = i;
            break;
        }
    }
    if (mailboxID == -1) {
        return -1;
    }
    mailboxes[mailboxID].id = mailboxID;
    mailboxes[mailboxID].numSlots = numSlots;
    mailboxes[mailboxID].slotSize = slotSize;
    mailboxes[mailboxID].usedSlots = 0;
    mailboxes[mailboxID].active = 1;
    return mailboxID;
}

int MboxRelease(int mailboxID) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || !mailboxes[mailboxID].active) {
        return -1;
    }
    mailboxes[mailboxID].active = 0;
    return 0;
}

int MboxSend(int mailboxID, void *message, int messageSize) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || !mailboxes[mailboxID].active || message == NULL || messageSize > mailboxes[mailboxID].slotSize) {
        return -1;
    }
    int slotIndex = -1;
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].mailboxID == -1) {
            slotIndex = i;
            break;
        }
    }
    if (slotIndex == -1) {
        processTable[getpid() % MAXPROC].blocked = 1;
        return -2;
    }
    mailSlots[slotIndex].mailboxID = mailboxID;
    mailSlots[slotIndex].size = messageSize;
    memcpy(mailSlots[slotIndex].message, message, messageSize);
    mailboxes[mailboxID].usedSlots++;
    return 0;
}

int MboxRecv(int mailboxID, void *message, int maxMessageSize) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || !mailboxes[mailboxID].active) {
        return -1;
    }
    int slotIndex = -1;
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].mailboxID == mailboxID) {
            slotIndex = i;
            break;
        }
    }
    if (slotIndex == -1) {
        processTable[getpid() % MAXPROC].blocked = 1;
        return -1;
    }
    int messageSize = mailSlots[slotIndex].size;
    if (messageSize > maxMessageSize) {
        return -1;
    }
    memcpy(message, mailSlots[slotIndex].message, messageSize);
    mailSlots[slotIndex].mailboxID = -1;
    mailboxes[mailboxID].usedSlots--;
    return messageSize;
}

int MboxCondSend(int mailboxID, void *message, int messageSize) {
    if (mailboxes[mailboxID].usedSlots >= mailboxes[mailboxID].numSlots) {
        return -2;
    }
    return MboxSend(mailboxID, message, messageSize);
}

int MboxCondRecv(int mailboxID, void *message, int maxMessageSize) {
    int foundMessage = 0;
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].mailboxID == mailboxID) {
            foundMessage = 1;
            break;
        }
    }
    if (!foundMessage) {
        return -2;
    }
    return MboxRecv(mailboxID, message, maxMessageSize);
}
