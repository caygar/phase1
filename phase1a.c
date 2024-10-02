#include "phase1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED 0
#define RUNNING 1
#define BLOCKED 2
#define ZAPPED 3


// Proc table struct
typedef struct procStruct {
    char name[MAXNAME];
    int pid;
    int ppid;
    int status;
    int (*startFunc)(void *);
    int priority;
    void *arg;
    USLOSS_Context context;
    int stackSize;
    char *stack;
    int hasQuit;   
    int quitStatus; 
    struct procStruct *nextSibling;
    struct procStruct *firstChild;
    struct procStruct *nextInQueue;
    struct procStruct *zappedBy[MAXPROC]; 
    int numZappers;
} procStruct;

// Instantiated Proc Table
procStruct procTable[MAXPROC];
procStruct *currentProc = NULL;
int nextPID = 1;


int findUnusedSlot(void);
procStruct *getProcessByPid(int pid);
static void processWrapper(void);

// Init: req func
void phase1_init(void) {
    for (int i = 0; i < MAXPROC; i++) {
        procTable[i].status = UNUSED;
    }

    // Special process: init
    procTable[0].pid = 1;
    strcpy(procTable[0].name, "init");
    procTable[0].priority = 6;
    procTable[0].status = RUNNING;
    procTable[0].ppid = 0; 
    procTable[0].hasQuit = 0;

    // Current process is init
    currentProc = &procTable[0]; 

    USLOSS_Console("phase1_init: Initialized process table and created init process\n");
}

// Helper
static void processWrapper(void) {
    currentProc->startFunc(currentProc->arg);
    USLOSS_Console("Error: Process %d returned from start function\n", currentProc->pid);
    USLOSS_Halt(1);
}

// Spork: req func
int spork(char *name, int (*func)(void *), void *arg, int stackSize, int priority) {
    if (priority < 1 || priority > 5 || stackSize < USLOSS_MIN_STACK || name == NULL || func == NULL) {
        return -1; 
    }

    int slot = findUnusedSlot();
    if (slot == -1) return -1;  

    procStruct *proc = &procTable[slot];
    proc->pid = nextPID++;
    proc->ppid = currentProc->pid;
    strncpy(proc->name, name, MAXNAME);
    proc->status = RUNNING;
    proc->startFunc = func;
    proc->arg = arg;
    proc->priority = priority;
    proc->stackSize = stackSize;
    proc->stack = malloc(stackSize);

    if (proc->stack == NULL) {
        return -2;  
    }

    USLOSS_ContextInit(&proc->context, proc->stack, proc->stackSize, NULL, processWrapper);

    USLOSS_Console("spork: Created process %s with PID %d\n", name, proc->pid);
    return proc->pid;
}

// Join: req func
int join(int *status) {
    procStruct *child = currentProc->firstChild;

    while (child != NULL) {
        if (child->hasQuit) {
            *status = child->quitStatus;
            USLOSS_Console("join: Joined with child PID %d\n", child->pid);
            free(child->stack);  
            child->status = UNUSED;  
            return child->pid;
        }
        child = child->nextSibling;
    }


    return -2; 
}

// Quit: req func
void quit_phase_1a(int status, int switchToPid) {
    if (currentProc->firstChild != NULL) {
        USLOSS_Console("quit_phase_1a: Error, process %d has unjoined children\n", currentProc->pid);
        USLOSS_Halt(1);
    }

    USLOSS_Console("quit_phase_1a: Process %d quitting with status %d\n", currentProc->pid, status);
    currentProc->hasQuit = 1;
    currentProc->quitStatus = status;

    TEMP_switchTo(switchToPid);
    
    USLOSS_Halt(0); 
}

// getpid: req func
int getpid(void) {
    return currentProc->pid;
}

// dump: req func
void dumpProcesses(void) {
    USLOSS_Console("PID\tName\t\tPPID\tPriority\tStatus\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (procTable[i].status != UNUSED) {
            USLOSS_Console("%d\t%s\t\t%d\t%d\t\t%d\n",
                           procTable[i].pid, procTable[i].name,
                           procTable[i].ppid, procTable[i].priority,
                           procTable[i].status);
        }
    }
}

// Helper
void TEMP_switchTo(int pid) {
    procStruct *newProc = getProcessByPid(pid);

    if (newProc == NULL || newProc->status == UNUSED) {
        USLOSS_Console("TEMP_switchTo: Invalid PID %d\n", pid);
        USLOSS_Halt(1);  
    }

    USLOSS_Console("TEMP_switchTo: Switching to process %d\n", pid);
    USLOSS_ContextSwitch(&currentProc->context, &newProc->context);
    currentProc = newProc;
}

// Helper
int findUnusedSlot(void) {
    for (int i = 0; i < MAXPROC; i++) {
        if (procTable[i].status == UNUSED) {
            return i;
        }
    }
    return -1;
}

// Helper
procStruct* getProcessByPid(int pid) {
    for (int i = 0; i < MAXPROC; i++) {
        if (procTable[i].pid == pid) {
            return &procTable[i];
        }
    }
    return NULL;
}



