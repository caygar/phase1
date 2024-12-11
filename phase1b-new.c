/**
 * Authors: Cumhur Aygar, Kavin Krisnaamani Janarthanan
 * Course: CSC 452 Fall 2024
 * Assignment: Phase 1b
 * File: phase1b.c
 * Description: 
 */

#include "phase1helper.h"
#include "phase1.h"
#include "usloss.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define RUNNABLE 0
#define RUNNING 1
#define ZOMBIE 2
#define BLOCKED_ON_JOIN 3
#define FREE 4
#define BLOCKED 5


typedef struct Process Process;
typedef struct RunQueue RunQueue;

/**
 * Struct to represent a process.
 * 
 * Includes information about the process such as pid, status, priority, name, start function,
 * argument, stack size, context, stack, parent, child, sibling, child count, and exit state.
 */
struct Process{  
    int pid; 
    int status;
    int priority;
    char name[MAXNAME];
    int (*startFunc)(char*);
    char *arg;
    int stackSize;
    USLOSS_Context context;
    void* stack;
    Process* parent;
    Process* child;
    Process* sibling;
    int childCount;
    int exitState;
    Process* nextInQueue; // for run queue
    int blockStatus; // for blockMe
};

/**
 * Struct to represent a run queue.
 * 
 * Includes information about the run queue such as the priority, 
 * the head of the queue, and the tail of the queue.
 */
struct RunQueue{
    int priority;
    Process* head;
    Process* tail;
};

Process processTable[MAXPROC];
Process* currentProcess;
RunQueue runQueue[6];
int PID;
int processCount;
clock_t lastSwitchTime;

/**
 * This helper function prints the run queue.
 */
void printRunQueue(){
    for (int i = 0; i < 6; i++) {
        USLOSS_Console("Priority: %d\n", i + 1);
        Process* process = runQueue[i].head;
        while (process != NULL) {
            USLOSS_Console("Process: %d, next: %d\n", process->pid, process->nextInQueue == NULL ? -1 : process->nextInQueue->pid);
            process = process->nextInQueue;
        }
    }
}

/**
 * This helper function assserts that the current call is made in kernel mode.
 * Halts the program if the call is made in user mode.
 * 
 * @param name - name of the function that is being called
 */
void assertKernelMode(char *name){
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call %s while in user mode!\n", name);
        USLOSS_Halt(1);
    }
}

/**
 * This helper function places a process in the run queue.
 * 
 * @param process - the process to be placed in the queue
 */
void placeInQueue(Process* process){
    int priority = process->priority - 1;
    if (runQueue[priority].head == NULL) {
        runQueue[priority].head = process;
        runQueue[priority].tail = process;
    } else {
        runQueue[priority].tail->nextInQueue = process;
        runQueue[priority].tail = process;

    }
}

/**
 * This helper function removes a process from the run queue.
 * 
 * @param process - the process to be removed from the queue
 */
void removeFromQueue(Process* process){
    int priority = process->priority - 1;
    runQueue[priority].head = process->nextInQueue;
    // if the process was the only one in the queue
    if (runQueue[priority].head == NULL) {
        runQueue[priority].tail = NULL;
    }
    process->nextInQueue = NULL;
}

/**
 * This helper function blocks the current process on a join.
 */
void blockJoin(){
    currentProcess->status = BLOCKED_ON_JOIN;
    removeFromQueue(currentProcess);
    dispatcher();
}

/**
 * This helper function returns the slot in the process table for the given pid.
 * 
 * @param pid - the pid of the process
 * 
 * @return slot - the slot in the process table
 */
int getSlot(int pid){
    int slot = pid % MAXPROC;
    return slot;
}

/**
 * This helper function finds the next available pid in the process table.
 */
void findPid(){
    if (processCount >= MAXPROC) {
        return;
    }
    while(processTable[PID % MAXPROC].status != FREE){
        PID++;
    }
}

/**
 * This helper function clears the process table slot.
 * 
 * @param slot - the slot in the process table
 */
void clearProcessTable(int slot){
    free(processTable[slot].stack);
    processTable[slot].pid = 0;
    processTable[slot].status = FREE;
    processTable[slot].priority = -1;
    processTable[slot].name[0] = '\0';
    processTable[slot].startFunc = NULL;
    processTable[slot].arg = NULL;
    processTable[slot].stackSize = -1;
    processTable[slot].stack = NULL;
    processTable[slot].parent = NULL;
    processTable[slot].child = NULL;
    processTable[slot].sibling = NULL;
    processTable[slot].childCount = 0;
    processTable[slot].exitState = 0;
    processTable[slot].nextInQueue = NULL;
    processTable[slot].blockStatus = 0;
    processCount--;
}

/**
 * This helper function disables interrupts.
 */
void disableInterrupts() {
	USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT);
}

/**
 * This helper function restores interrupts.
 */
void restoreInterrupts() {
	USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);
}

/**
 * This function switches to the process with the given pid.
 * 
 * @param newpid - the pid of the process to switch to
 */
void switchTo(int newpid){
    // return if newpid is the same as the current process
    if (newpid == getpid()) {
        return;
    }
    // return if newpid is invalid
    if (processTable[getSlot(newpid)].status == FREE) {
        return;
    }   
    // switch to new process
    if (currentProcess == NULL) {
        currentProcess = &processTable[getSlot(newpid)];
        currentProcess->status = RUNNING;
        USLOSS_ContextSwitch(NULL, &currentProcess->context);
        return;
    }
    // switch to new process
    Process* oldProcess = currentProcess;
    oldProcess->nextInQueue = NULL;
    if (oldProcess->status == RUNNING) {
        removeFromQueue(oldProcess);
        oldProcess->status = RUNNABLE;
        placeInQueue(oldProcess);
    }
    
    currentProcess = &processTable[getSlot(newpid)];
    currentProcess->status = RUNNING;
    USLOSS_ContextSwitch(&oldProcess->context, &currentProcess->context);
    lastSwitchTime = clock();
}

/**
 * This function initializes the process table and the first process.
 */
void phase1_init(void) {
    assertKernelMode("phase1_init");

    // Initialize the process table
    for (int i = 0; i < MAXPROC; i++) {
        clearProcessTable(i);
    }

    // Initialize the run queue
    for (int i = 0; i < 6; i++) {
        runQueue[i].priority = i + 1;
        runQueue[i].head = NULL;
        runQueue[i].tail = NULL;
    }

    // Initialize the first process
    PID = 1;
    processCount = 0;

    int slot = getSlot(PID);
    processTable[slot].pid = PID;
    processTable[slot].status = RUNNABLE;
    processTable[slot].priority = 6;
    strcpy(processTable[slot].name, "init");
    processTable[slot].startFunc = &init_main;
    processTable[slot].stackSize = USLOSS_MIN_STACK;
    processTable[slot].stack = malloc(USLOSS_MIN_STACK);
    processCount++;

    // initialize context
    placeInQueue(&processTable[slot]);
    russ_ContextInit(PID, &processTable[slot].context, processTable[slot].stack, 
    processTable[slot].stackSize, processTable[slot].startFunc, processTable[slot].arg);

    PID++;
}

/**
 * This function starts up a new process, which is a child of the current process.
 * 
 * @param name - the name of the process to be created
 * @param startFunc - the main function for the child process
 * @param arg - the argument to be passed to the start function, may be NULL
 * @param stackSize - the size of the stack for the child process
 * @param priority - the priority of the child process, range from 1 to 5
 * 
 * @return -1 if the process table is full, -2 if the stack size is too small, 
 *         -1 if the priority is out of range, -1 if the start function is NULL, 
 *         -1 if the name is NULL, -1 if the name is too long, or the pid of the 
 *          child process
 */
int spork(char *name, int (*startFunc)(char*), char *arg, int stackSize, int priority){
    // check if in kernel mode and disable interrupts
    assertKernelMode("spork");
    disableInterrupts();

    // check stack size
    if (stackSize < USLOSS_MIN_STACK) {
        return -2;
    }
    // check process count
    if (processCount >= MAXPROC) {
        return -1;
    }
    // check priority range
    if (priority < 1 || priority > 6) {
        return -1;
    }
    // check if startFunc is NULL or name is NULL
    if (startFunc == NULL || name == NULL) {
        return -1;
    }
    // check if name is too long
    if (strlen(name) > MAXNAME) {
        return -1;
    }

    findPid();
    int slot = getSlot(PID);

    processTable[slot].pid = PID;
    processTable[slot].status = RUNNABLE;
    processTable[slot].priority = priority;
    strcpy(processTable[slot].name, name);
    processTable[slot].startFunc = startFunc;
    processTable[slot].arg = arg;
    processTable[slot].stackSize = stackSize;
    processTable[slot].stack = malloc(stackSize);
    processTable[slot].parent = currentProcess;
    processTable[slot].sibling = NULL;
    processTable[slot].childCount = 0;
    processCount++;

    // initialize context
    russ_ContextInit(PID, &processTable[slot].context, processTable[slot].stack, processTable[slot].stackSize, processTable[slot].startFunc, processTable[slot].arg);
    
    placeInQueue(&processTable[slot]);

    // set current process
    if (currentProcess->child != NULL){
        processTable[slot].sibling = currentProcess->child;
    }

    currentProcess->child = &processTable[slot];
    currentProcess->childCount++;

    PID++;

    dispatcher();

    restoreInterrupts();
    return processTable[slot].pid;
}

/**
 * This function joins the child process to the parent process.
 * 
 * @param status - the exit status of the child process
 * 
 * @return -1 if the status is NULL, -2 if there are no children, 
 *          else the pid of the child process joined
 */
int join(int *status){
    assertKernelMode("join");
    disableInterrupts();
    
    // check if status is NULL
    if (status == NULL) {
        return -3;
    }
    // check if there are no children TODO: check if this is correct (how to know if all children are joined?)
    if (currentProcess->childCount == 0) {
        return -2;
    }
    
    Process* deadChild = currentProcess->child;
    Process* toRemove = NULL;

    //USLOSS_Console("Child %s state: %d\n", deadChild->name, deadChild->status);
    // check if any children are zombies
    while(deadChild != NULL && deadChild->status != ZOMBIE) {
        deadChild = deadChild->sibling;
        //USLOSS_Console("Child %s state: %d\n", deadChild->name, deadChild->status);
    }
    
    // if no children are zombies, block the current process
    if (deadChild == NULL) {
        blockJoin();
        // find the zombie child
        deadChild = currentProcess->child;
        while(deadChild->sibling != NULL && deadChild->status != ZOMBIE) {
            deadChild = deadChild->sibling;
        }
    }

    toRemove = deadChild;

    if (currentProcess->child == toRemove) {
        currentProcess->child = toRemove->sibling;
    } else {
        Process* curr = currentProcess->child;
        while (curr->sibling != toRemove) {
            curr = curr->sibling;
        }
        curr->sibling = toRemove->sibling;
    }

    currentProcess->childCount--;
    *status = toRemove->exitState;

    // remove child from process table
    int removedPid = toRemove->pid;
    int slot = getSlot(toRemove->pid);
    clearProcessTable(slot);  
    
    dispatcher();
    
    restoreInterrupts();
    return removedPid;
}

/**
 * This function quits the current process with the given exit status.
 * 
 * @param status - the exit status of the current process
 */
void quit(int status){
    assertKernelMode("quit");
    disableInterrupts();

    // check if current process has children
    if (currentProcess->childCount > 0) {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", getpid());
        USLOSS_Halt(1);
    }

    Process* parent = currentProcess->parent;

    if (parent == NULL) {
        USLOSS_Console("ERROR: Process pid %d called quit() with no parent.\n", getpid());
        USLOSS_Halt(1);
    }

    if (currentProcess->parent->status == BLOCKED_ON_JOIN) {
        currentProcess->parent->status = RUNNABLE;
        placeInQueue(currentProcess->parent);
    } 
    
    currentProcess->status = ZOMBIE;
    currentProcess->exitState = status;
    
    removeFromQueue(currentProcess);
    dispatcher();
    
    restoreInterrupts();
}


/**
 * This function returns the pid of the current process.
 * 
 * @return the pid of the current process, 0 if there is no current process
 */
int getpid(void){
    assertKernelMode("getpid");
    if (currentProcess == NULL) {
        return 0;
    }
    return currentProcess->pid;
}

/**
 * This function prints the process table.
 */
void dumpProcesses(void){
    USLOSS_Console(" PID  PPID  NAME              PRIORITY  STATE\n");
    for (int i = 0; i < MAXPROC; i++) {
        Process *slot = &processTable[i];
        if (slot->status == FREE) {
            continue;
        }
        USLOSS_Console("%4d  %4d  %-17s %-8d  ", slot->pid, slot->parent == NULL ? 0 : slot->parent->pid, slot->name, slot->priority);
        if (slot->status == ZOMBIE) {
            USLOSS_Console("Terminated(%d)\n", slot->exitState);
        } else if (slot->status == BLOCKED_ON_JOIN) {
            USLOSS_Console("Blocked(waiting for child to quit)\n");
        }
        else if (slot->status == BLOCKED) {
            USLOSS_Console("Blocked(%d)\n", slot->blockStatus);
        }
        else {
            USLOSS_Console("%s\n", slot->status == RUNNABLE ? "Runnable" : "Running");
        }
    }
}

/**
 * This function blocks the current process with the given block status.
 * 
 * @param block_status - the status to block the process with
 */
void blockMe(int block_status){
    assertKernelMode("blockMe");
    disableInterrupts();

    if (block_status <= 10){
        USLOSS_Console("ERROR: block_status must be greater than 10.\n");
        USLOSS_Halt(1);
    }
    currentProcess->status = BLOCKED;
    currentProcess->blockStatus = block_status;
    removeFromQueue(currentProcess);
    dispatcher();

    restoreInterrupts();
}

/**
 * This function unblocks the process with the given pid.
 * 
 * @param pid - the pid of the process to unblock
 * 
 * @return -2 if the pid is invalid, the process is not blocked or the 
 *              blockStatus is less than or equal to 10
 *          0 if the process was unblocked
 */
int unblockProc(int pid){
    assertKernelMode("unblockProc");
    disableInterrupts();

    int slot = getSlot(pid);
    Process* process = &processTable[slot];
    if (process->status == FREE || process->status != BLOCKED || process->blockStatus <= 10) {
        return -2;
    }
    process->status = RUNNABLE;
    process->blockStatus = 0;
    placeInQueue(process);
    dispatcher();

    restoreInterrupts();
    return 0;
}

/**
 * This function decides which process to run next.
 */
void dispatcher(void){
    // find the highest priority process
    disableInterrupts();
    int i = 0;
    while(runQueue[i].head == NULL && i < 6){
        i++;
    }
    Process* highestPriority = runQueue[i].head;
    
    if (highestPriority == NULL) {
        USLOSS_Console("ERROR: dispatcher() called with no runnable processes.\n");
        USLOSS_Halt(1);
    }
    if (currentProcess == NULL){
        if (highestPriority->priority == 6){switchTo(highestPriority->pid); return;}
        else { 
            USLOSS_Console("ERROR: dispatcher() called while current process is NULL and highest priority is not 6.\n");
            USLOSS_Halt(1);
        }
    }
        
    double timeElapsed = (clock() - lastSwitchTime) / (double) CLOCKS_PER_SEC * 1000;
    
    if (highestPriority->pid != currentProcess->pid) {
        switchTo(highestPriority->pid);
    }

    // if processes with same priority, switch if it has been more than 80ms
    else if (highestPriority->nextInQueue != NULL && timeElapsed > 80) {
        switchTo(highestPriority->nextInQueue->pid);
    }
    restoreInterrupts();
}


