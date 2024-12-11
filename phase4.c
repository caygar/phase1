/**
 * Authors: Kavin Krisnaamani Janarthanan, Cumhur Aygar
 * Course: CSC 452 - Fall 2024
 * Assignment: Phase 4b
 * File: phase4.c
 * Description: Implementation of the phase 4 system calls, sleep, termRead/termWrite,
 *              and diskSize/diskRead/diskWrite for clock and terminal devices. 
 */

#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <phase4_usermode.h>
#include <usloss.h>
#include <usyscall.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define FREE 0
#define ASLEEP 1
#define AWAKE 2

// CLOCK DEVICE
// Struct to store sleep requests
typedef struct SleepRequest SleepRequest; 
typedef struct SleepRequest {
    int seconds;
    int mbox; 
    int status;
    SleepRequest* next;
};

SleepRequest* sleepRequestQueue; // Queue of sleep requests
SleepRequest sleepRequestsTable[MAXPROC]; // Table of sleep requests

// Sleep request functions
void sleep(USLOSS_Sysargs *args);
int sleepMain(char *args);

// TERMINAL DEVICE
// arrays to store terminal information
int termWriteMutex[USLOSS_TERM_UNITS];
int termToWrite[USLOSS_TERM_UNITS];
int termReadMbox[USLOSS_TERM_UNITS];
int termLineIndex[USLOSS_TERM_UNITS];
char termLines[USLOSS_TERM_UNITS][MAXLINE+1];

// Terminal request functions
void termWrite(USLOSS_Sysargs *args);
void termRead(USLOSS_Sysargs *args);
int termMain(char *args);

// DISK DEVICE
// arrays to store disk information
typedef struct DiskRequest DiskRequest;
struct DiskRequest {
    int pid;
    int track;
    int block;
    int sectors;
    int unit;
    void* buffer;
    int operation;
    int mbox;
    DiskRequest* next;
};
DiskRequest diskRequestTable[MAXPROC];
int diskMutex[USLOSS_DISK_UNITS];
int diskQueueMutex[USLOSS_DISK_UNITS];
int diskDaemonMutex[USLOSS_DISK_UNITS];
int diskTrackMutex[USLOSS_DISK_UNITS];
int diskTrackNum;
DiskRequest* diskQueue[USLOSS_DISK_UNITS];


void diskRead(USLOSS_Sysargs *args);
void diskWrite(USLOSS_Sysargs *args);
void diskSize(USLOSS_Sysargs *args);
void addToDiskQueue(int unit, int pid);
void seekDisk(int unit, int track);
int diskMain(char* args);


/**
 * @brief Initializes the phase 4 system calls, sleep and 
 * termRead/termWrite for clock and terminal devices.
 */
void phase4_init(void) {
    // Initialize system call vector
    systemCallVec[SYS_SLEEP] = sleep;
    systemCallVec[SYS_TERMREAD]  = termRead;
    systemCallVec[SYS_TERMWRITE] = termWrite;
    systemCallVec[SYS_DISKREAD] = diskRead;
    systemCallVec[SYS_DISKWRITE] = diskWrite;
    systemCallVec[SYS_DISKSIZE] = diskSize;

    // Initialize sleep request table
    for (int i = 0; i < MAXPROC; i++) {
        sleepRequestsTable[i].status = FREE;
        sleepRequestsTable[i].next = NULL;
        sleepRequestsTable[i].mbox = -1;
    }
    sleepRequestQueue = NULL;

    // Initialize terminal arrays
    memset(termLines, '\0', sizeof(termLines));
    memset(termLineIndex, 0, sizeof(termLineIndex));
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, i, (void*)(long)0x2);
        termReadMbox[i] = MboxCreate(10, MAXLINE);
        termToWrite[i] = MboxCreate(1, 0);
        termWriteMutex[i] = MboxCreate(1, 0);
    }

    // Initialize disk arrays
    for (int i = 0; i < MAXPROC; i++) {
        diskRequestTable[i].mbox = MboxCreate(1, 0);
        diskRequestTable[i].next = NULL;
        diskRequestTable[i].pid = -1;
    }

    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        diskQueue[i] = NULL;
        diskMutex[i] = MboxCreate(1, 0);
        diskQueueMutex[i] = MboxCreate(1, 0);
        diskDaemonMutex[i] = MboxCreate(1, 0);
        diskTrackMutex[i] = MboxCreate(1, 0);
        diskQueue[i] = NULL;
    }
}

/**
 * @brief Starts the deamons for phase 4 by spawning the sleepMain and termMain processes.
 */
void phase4_start_service_processes(void){
    // Start the clock deamon
    int sleepServicePid = spork("sleepMain", sleepMain, NULL, USLOSS_MIN_STACK, 2);

    // Start the terminal deamons
    for (int i = 0; i < USLOSS_TERM_UNITS; i++) {
        char name[128];
        sprintf(name, "termMain%d", i);
        char unit[10];
        sprintf(unit, "%d", i);
        int termPID = spork(name, termMain, unit, USLOSS_MIN_STACK, 2);
    }

    // Start the disk deamons
    for (int i = 0; i < USLOSS_DISK_UNITS; i++) {
        char name[128];
        sprintf(name, "diskMain%d", i);
        char unit[10];
        sprintf(unit, "%d", i);

        int diskPID = spork(name, diskMain, unit, USLOSS_MIN_STACK, 2);
    }
}

/**
 * @brief System call for sleeping for a given number of seconds.
 * 
 * @param args: The arguments sent to the sleep system call. 
 *              Specifically, the number of seconds to sleep.
 */
void sleep(USLOSS_Sysargs *args) {
    // Get the number of seconds to sleep
    int seconds = (long) args->arg1;
    
    // Check if the number of seconds is valid
    if (seconds < 0) {
        args->arg4 = (void *) -1;
        return;
    }

    // Find a free slot in the sleep request table
    int sleepIndex = -1;
    for (int i = 0; i < MAXPROC; i++) {
        if (sleepRequestsTable[i].status == FREE) {
            sleepIndex = i;
            break;
        }
    }

    // Fill the sleep request table
    SleepRequest* request = &sleepRequestsTable[sleepIndex];
    request->seconds = currentTime() + seconds * 1000000;
    request->mbox = MboxCreate(1, 0);
    request->status = ASLEEP;
    
    // Add the request to the sleep request queue
    SleepRequest* rest = sleepRequestQueue;
    request->next = rest; 
    sleepRequestQueue = request; 
    
    MboxRecv(request->mbox, NULL, 0);

    args->arg4 = (void *) 0;
}

/**
 * @brief The main function for the sleep system call. 
 * It checks the sleep request queue and wakes up the processes that are asleep
 * if their sleep time has passed.
 * 
 * @param args: The arguments for the sleep system call.
 * @return int: 0 when the process is done.
 */
int sleepMain(char* args) {
    int status;
    while(1){
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        SleepRequest* cur = sleepRequestQueue;
        // Iterate through the sleep request queue
        while (cur != NULL) {
            // Check if the process is asleep and if it is time to wake up
            if (cur->status == ASLEEP && currentTime() > cur->seconds){
                cur->status = AWAKE;
                MboxSend(cur->mbox, NULL, 0);
            }
            cur = cur->next;
        }
    }
    return 0;
}

/**
 * @brief System call for writing characters of a given buffer to a terminal.
 * The writing operation is done character by character, and each operation
 * is atomic.
 * 
 * @param args: The arguments for the TermWrite system call.
 */
void termWrite(USLOSS_Sysargs *args) {
    char *buffer = (char *) args->arg1;
    int bufferSize = (int) args->arg2;
    int unit = (int) args->arg3;

    // invalid input
    if (buffer == NULL || bufferSize<= 0 || unit < 0 || unit >= USLOSS_TERM_UNITS) {
        args->arg4 = (void *) -1;
        return;
    }

    // enter mutex
    MboxSend(termWriteMutex[unit], NULL, 0);

    for (int i = 0; i < bufferSize; i++) {
        // wait for terminal to be ready
        MboxRecv(termToWrite[unit], NULL, 0);

        int cr_val = 0x1;
        cr_val |= 0x2;
        cr_val |= 0x4;
        cr_val |= (buffer[i] << 8);

        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long) cr_val);
    }

    args -> arg4 = (void *) 0;
    args -> arg2 = (void *) bufferSize;

    // exit mutex
    MboxRecv(termWriteMutex[unit], NULL, 0);

}

/**
 * @brief System call for reading characters from a terminal.
 * 
 * 
 * @param args: The arguments for the TermRead system call.
 */
void termRead(USLOSS_Sysargs *args) {
    char *buffer = (char *) args->arg1;
    int bufferSize = (int) args->arg2;
    int unit = (int) args->arg3;

    if (buffer == NULL || bufferSize <= 0 || unit < 0 || unit >= USLOSS_TERM_UNITS) {
        args->arg4 = (void *) -1;
        return;
    }

    char line[MAXLINE+1];

    // wait for a line to be read
    int len = MboxRecv(termReadMbox[unit], &line, MAXLINE+1);

    if (len > bufferSize) {
        len = bufferSize;
    }

    strncpy(buffer, line, len);

    args->arg4 = (void *) 0;
    args->arg2 = (void *) len;
}


/**
 * @brief The main function for the terminal system calls.
 * 
 * @param args: The terminal unit number as a string.
 * @return int 
 */
int termMain(char *args) {
    int status;

    int unit = atoi(args);

    while(1) {
        // wait for terminal unit to be ready
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        int recv = USLOSS_TERM_STAT_RECV(status);
        int xmit = USLOSS_TERM_STAT_XMIT(status);

        if (xmit == USLOSS_DEV_READY ) {
            // signal that the terminal is ready
            MboxCondSend(termToWrite[unit], NULL, 0);
        } else if (xmit == USLOSS_DEV_ERROR) {
            USLOSS_Console("USLOSS_DEV_ERROR. Halting...\n");
            USLOSS_Halt(1);
        }

        if (recv == USLOSS_DEV_BUSY) {
            // read the character
            char character = USLOSS_TERM_STAT_CHAR(status);
            
            if (termLineIndex[unit] < MAXLINE) {
                termLines[unit][termLineIndex[unit]] = character;
                termLineIndex[unit]++;
            }
            // check if the character is a newline
            if (character == '\n') {
                // send the line to the mailbox
                MboxCondSend(termReadMbox[unit], termLines[unit], termLineIndex[unit]);
                memset(termLines[unit], '\0', sizeof(termLines[unit]));
                termLineIndex[unit] = 0;
            }

            // check if the line is full, if so add character to new line after sending the current line
            if (termLineIndex[unit] == MAXLINE) {
                MboxCondSend(termReadMbox[unit], termLines[unit], termLineIndex[unit]);
                memset(termLines[unit], '\0', sizeof(termLines[unit]));
                termLineIndex[unit] = 0;

                termLines[unit][termLineIndex[unit]] = character;
                termLineIndex[unit]++;
            }




        } else if (recv == USLOSS_DEV_ERROR) {
            USLOSS_Console("An error occurred on unit %d\n", unit);
            USLOSS_Halt(1);
        }

    }

    return 0;
}

/**
 * @brief System call for reading from the disk.
 * 
 * @param args: The arguments for the diskRead system call.
 */
void diskRead(USLOSS_Sysargs *args) {
    void* buffer = args->arg1;
    int sectorNumber = (int) args->arg2;
    int trackStart = (int) args->arg3;
    int blockStart = (int) args->arg4;
    int unit = (int) args->arg5;

    if (sectorNumber < 0 || trackStart < 0 || blockStart < 0 || unit < 0 || unit >= USLOSS_DISK_UNITS) {
        args->arg4 = (void *) -1;
        return;
    }

    int pid = getpid();

    //acquire the mutex for the queue
    MboxSend(diskQueueMutex[unit], NULL, 0);

    diskRequestTable[pid % MAXPROC].pid = pid;
    diskRequestTable[pid % MAXPROC].track = trackStart;
    diskRequestTable[pid % MAXPROC].block = blockStart;
    diskRequestTable[pid % MAXPROC].sectors = sectorNumber;
    diskRequestTable[pid % MAXPROC].unit = unit;
    diskRequestTable[pid % MAXPROC].buffer = buffer;
    diskRequestTable[pid % MAXPROC].operation = USLOSS_DISK_READ;

    addToDiskQueue(unit, pid);

    // release the mutex for the queue
    MboxRecv(diskQueueMutex[unit], NULL, 0);

    MboxCondSend(diskMutex[unit], NULL, 0);
    MboxRecv(diskRequestTable[pid % MAXPROC].mbox, NULL, 0);

    args->arg1 = (void *)(long) 0;
    args->arg4 = (void *)(long) 0;
}

/**
 * @brief System call for writing to the disk.
 * 
 * @param args: The arguments for the diskWrite system call.
 */
void diskWrite(USLOSS_Sysargs *args) {
    void* buffer = args->arg1;
    int sectorNumber = (int) args->arg2;
    int trackStart = (int) args->arg3;
    int blockStart = (int) args->arg4;
    int unit = (int) args->arg5;

    // invalid input
    if (trackStart < 0 || blockStart < 0 || blockStart >= USLOSS_DISK_TRACK_SIZE || unit < 0 || unit >= USLOSS_DISK_UNITS) {
        args->arg4 = (void *) -1;
        return;
    }

    

    int pid = getpid();

    //acquire the mutex for the queue
    MboxSend(diskQueueMutex[unit], NULL, 0);

    // add it to the request table
    diskRequestTable[pid % MAXPROC].pid = pid;
    diskRequestTable[pid % MAXPROC].track = trackStart;
    diskRequestTable[pid % MAXPROC].block = blockStart;
    diskRequestTable[pid % MAXPROC].sectors = sectorNumber;
    diskRequestTable[pid % MAXPROC].unit = unit;
    diskRequestTable[pid % MAXPROC].buffer = buffer;
    diskRequestTable[pid % MAXPROC].operation = USLOSS_DISK_WRITE;

    addToDiskQueue(unit, pid);

    MboxRecv(diskQueueMutex[unit], NULL, 0);

    MboxCondSend(diskMutex[unit], NULL, 0);
    MboxRecv(diskRequestTable[pid % MAXPROC].mbox, NULL, 0);
    args->arg1 = (void *)(long) 0;
    args->arg4 = (void *)(long) 0;

    if ((trackStart >= 16 && unit == 0) || (trackStart >= 32 && unit == 1)){
        args->arg1 = (void *)(long) USLOSS_DEV_ERROR;
        return;
    }
    
}

/**
 * @brief System call for getting the size of the disk.
 * 
 * @param args: The arguments for the diskSize system call.
 */
void diskSize(USLOSS_Sysargs *args) {
    int unit = (int) args->arg1;

    MboxRecv(diskTrackMutex[unit], NULL, 0);

    args->arg1 = (void *)(long) USLOSS_DISK_SECTOR_SIZE;
    args->arg2 = (void *)(long) USLOSS_DISK_TRACK_SIZE;
    args->arg3 = (void *)(long) diskTrackNum;
    args->arg4 = (void *)(long) 0;
}

/**
 * @brief The mian function for the disk system calls.
 * 
 * @param args: The unit number of the disk as a string.
 * @return int 
 */
int diskMain(char* args){
    int unit = atoi(args);
    int status;
    USLOSS_DeviceRequest request;

    request.opr = USLOSS_DISK_TRACKS;
    request.reg1 = (void*)(long)&diskTrackNum;
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
    waitDevice(USLOSS_DISK_DEV, unit, &status);

    // acquire lock on this disk
    MboxSend(diskTrackMutex[unit], NULL, 0);
    while (1){
        // wait for a request
        MboxRecv(diskMutex[unit], NULL, 0);

        DiskRequest** curr = &diskQueue[unit];
        while (*curr != NULL){
            DiskRequest* diskReq = *curr;
            
            int track = diskReq->track;
            int sectors = diskReq->sectors;
            int block = diskReq->block;
            void *buffer = diskReq->buffer;
            int operation = diskReq->operation;
            int mbox = diskReq->mbox;

            seekDisk(unit, track);

            request.opr = operation;
            request.reg1 = (void*)(long)block;
            request.reg2 = buffer;

            for (int i = 0; i < sectors; i++){
                // reset the block if it reaches the end of the track
                if ((int)(long)request.reg1 == USLOSS_DISK_TRACK_SIZE){
                    request.reg1 = (void*)(long)0;
                    track++;
                    seekDisk(unit, track);
                }
                MboxSend(diskDaemonMutex[unit], NULL, 0);

                USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
                waitDevice(USLOSS_DISK_DEV, unit, &status);

                MboxRecv(diskDaemonMutex[unit], NULL, 0);

                request.reg1++;
                request.reg2 += USLOSS_DISK_SECTOR_SIZE;
            }
            MboxSend(diskQueueMutex[unit], NULL, 0);

            *curr = diskReq->next;
            diskReq->next = NULL;

            MboxRecv(diskQueueMutex[unit], NULL, 0);

            MboxSend(mbox, NULL, 0);
        }
    }
    return 0;
}

/**
 * @brief Helper function to seek the disk to a given track.
 * 
 * @param unit: The unit number of the disk.
 * @param track: The track number to seek to.
 */
void seekDisk(int unit, int track){
    int res;
    int status;

    USLOSS_DeviceRequest request;
    request.opr = USLOSS_DISK_SEEK;
    request.reg1 = (void*)(long)track;

    MboxSend(diskDaemonMutex[unit], NULL, 0);

    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &request);
    waitDevice(USLOSS_DISK_DEV, unit, &status);

    MboxRecv(diskDaemonMutex[unit], NULL, 0);
}

/**
 * @brief Helper function to add a disk request to the disk request queue.
 * 
 * @param unit: The unit number of the disk.
 * @param pid: The process id of the process making the request.
 */
void addToDiskQueue(int unit, int pid){
    DiskRequest* request = NULL;

    // if the queue is empty, the request is the head
    if (diskQueue[unit] == NULL) {
        diskQueue[unit] = &diskRequestTable[pid % MAXPROC];
        return;
    }
    request = diskQueue[unit];
    int currTrack = request->track;
    int tableTrack = diskRequestTable[pid % MAXPROC].track;

    if (currTrack <= tableTrack) {
        while (request->next != NULL && tableTrack >= request->next->track && request->next->track >= currTrack) {
            request = request->next;
        }
        diskRequestTable[pid % MAXPROC].next = request->next;
        request->next = &diskRequestTable[pid % MAXPROC];
    } else {
        while (request->next != NULL && currTrack <= request->next->track) {
            request = request->next;
        }

        if (request->next == NULL) {
            request->next = &diskRequestTable[pid % MAXPROC];
        } else {
            while (request->next != NULL && tableTrack >= request->next->track) {
                request = request->next;
            }
            diskRequestTable[pid % MAXPROC].next = request->next;
            request->next = &diskRequestTable[pid % MAXPROC];
        }
    }

}
