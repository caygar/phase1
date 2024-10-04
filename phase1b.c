#include <stdio.h>
#include <stdlib.h>
#include <usloss.h>
#include "phase1.h"

#define TIME_SLICE 80000  // 80ms time-slice in microseconds

int init(void *arg);
void sporkWrapper(void *arg);
unsigned int disableInterruptsCustom(void);
void restoreInterruptsCustom(unsigned int old_psr);


typedef struct Process {
    int pid;
    char name[MAXNAME];
    USLOSS_Context context;
    int status;
    int priority;
    int isActive;
    void *stack;
    int (*startFunc)(void *);
    void *arg;
    int hasQuit;
    int quitStatus;
    int ppid;
    struct Process *nextSibling;
    struct Process *firstChild;
    int timeUsed;  
    int isBlocked;  
} Process;

typedef struct ProcessQueue {
    Process *head;
    Process *tail;
} ProcessQueue;

ProcessQueue readyQueue[6];  

char initStack[USLOSS_MIN_STACK * 2];

int initProc = 1;

Process processTable[MAXPROC];
int nextPID = 2;
int currPID = 1;

void check_kernel_mode() {
    int psr = USLOSS_PsrGet();
    if (!(psr & USLOSS_PSR_CURRENT_MODE)) {
        USLOSS_Console("ERROR: Someone attempted to call this function while in user mode!\n");
        USLOSS_Halt(1);
    }
}

void phase1_init(void) {
    for (int i = 0; i < MAXPROC; i++) {
        processTable[i].pid = -1;
        processTable[i].isActive = 0;
        processTable[i].priority = -1;
        processTable[i].status = 0;
        processTable[i].stack = NULL;
        processTable[i].hasQuit = 0;
        processTable[i].quitStatus = -1;
        processTable[i].ppid = -1;
        processTable[i].firstChild = NULL;
        processTable[i].nextSibling = NULL;
        processTable[i].timeUsed = 0;
        processTable[i].isBlocked = 0;
    }

    processTable[0].pid = 1;
    snprintf(processTable[0].name, MAXNAME, "%s", "Init");
    processTable[0].isActive = 1;
    processTable[0].priority = 6;
    processTable[0].stack = initStack;

    nextPID = 2;
    
    USLOSS_ContextInit(&(processTable[0].context), initStack, 2 * USLOSS_MIN_STACK, NULL, init);
    
    enqueue(&processTable[0]);
}


int init(void *arg) {
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();
    int pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);

    dispatcher();  
    USLOSS_Halt(0);
    return 0;
}

int spork(char *name, int (*func)(void *), void *arg, int stacksize, int priority) {
    USLOSS_Console("--------IN SPORK-------\n");
    check_kernel_mode();
    unsigned int old_psr = disableInterruptsCustom();

    if (stacksize < USLOSS_MIN_STACK) {
        return -2;
    }

    if (priority < 1 || priority > 5 || func == NULL || name == NULL || strlen(name) >= MAXNAME) {
        return -1;
    }

    int pid = nextPID;
    int slot = pid % MAXPROC - 1;
    
    while (processTable[slot].isActive) {
        pid++;
        slot = pid % MAXPROC;
        if (pid - nextPID > MAXPROC) {
            return -1;
        }
    }

    void *stack = malloc(stacksize);
    processTable[slot].pid = pid;
    processTable[slot].isActive = 1;
    processTable[slot].priority = priority;
    snprintf(processTable[slot].name, MAXNAME, "%s", name);
    processTable[slot].stack = stack;
    processTable[slot].startFunc = func;
    processTable[slot].arg = arg;
    processTable[slot].ppid = currPID;
    processTable[slot].timeUsed = 0; 

    if (processTable[currPID - 1].firstChild == NULL) {
        processTable[currPID - 1].firstChild = &processTable[slot];
    } else {
        Process *child = processTable[currPID - 1].firstChild;
        while (child->nextSibling != NULL) {
            child = child->nextSibling;
        }
        child->nextSibling = &processTable[slot];
    }

    USLOSS_ContextInit(&(processTable[slot].context), stack, stacksize, NULL, sporkWrapper);
    nextPID++;

    enqueue(&processTable[slot]);  

    dispatcher();
    restoreInterruptsCustom(old_psr);
    return processTable[slot].pid;
}

void sporkWrapper(void *arg) {
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    int slot = (currPID % MAXPROC) - 1;
    int (*func)(void *) = processTable[slot].startFunc;
    void *funcArg = processTable[slot].arg;

    int status = func(funcArg);
    if (currPID == 1) {
        
        return; 
    } else {
        printProcess(currPID);
        quit(status);
    }
}

int join(int *status) {
    check_kernel_mode();  
    unsigned int old_psr = disableInterruptsCustom();

    //USLOSS_Console("ENTERING JOIN WITH PID: %d\n", currPID);

    if (status == NULL) {
        restoreInterruptsCustom(old_psr);
        return -3;  
    }

    Process *current = &processTable[currPID - 1];
    Process *child = current->firstChild;

    if (child == NULL) {
        restoreInterruptsCustom(old_psr);
        return -2;  
    }

    while (child != NULL) {
        if (child->hasQuit) {
            *status = child->quitStatus;  
            child->isActive = 0;  
            child->hasQuit = 0;  
            free(child->stack);  
            child->stack = NULL;

            restoreInterruptsCustom(old_psr);
            return child->pid;  
        }
        child = child->nextSibling;
    }

    current->isActive = 0;  
    dispatcher();  

    restoreInterruptsCustom(old_psr);
    return -2;
}

void quit(int status) {
    unsigned int old_psr = disableInterruptsCustom();
    check_kernel_mode();  // Ensure kernel mode
    USLOSS_Console("Entering quit with PID: %d\n", currPID);

    Process *current = &processTable[currPID - 1];
    USLOSS_Console("PROCESS BEFORE QUIT\n");
    printProcess(current->pid);

    // Check if the current process has any active (running) children
    Process *child = current->firstChild;

    while (child != NULL) {
        // Check if the child is still active and hasn't quit
        if (child->isActive && !child->hasQuit) {
            USLOSS_Console("ERROR: Process %d has active children. Cannot quit.\n", current->pid);
            USLOSS_Halt(1);  // Halt the simulation with error
        }
        child = child->nextSibling;  // Move to the next sibling
    }

    // Mark current process as having quit
    current->hasQuit = 1;
    current->quitStatus = status;
    current->isActive = 0;

    // No need to remove the child from the parent's child list
    USLOSS_Console("Process After Quit\n");
    printProcess(current -> pid);
    USLOSS_Console("Leaving quit with PID: %d\n", currPID);
    dumpProcesses();

    dispatcher();  // Switch to another process

    restoreInterruptsCustom(old_psr);
}




void zap(int pid) {
    check_kernel_mode();
    unsigned int old_psr = disableInterruptsCustom();

    if (pid == currPID || pid == 1) {
        USLOSS_Console("ERROR: Invalid zap target.\n");
        USLOSS_Halt(1);
    }

    Process *target = NULL;
    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].pid == pid) {
            target = &processTable[i];
            break;
        }
    }

    if (target == NULL || !target->isActive) {
        USLOSS_Console("ERROR: Attempted to zap non-existent or terminated process.\n");
        USLOSS_Halt(1);
    }

    Process *current = &processTable[currPID - 1];
    current->isActive = 0;

    while (!target->hasQuit) {
        dispatcher();  
    }

    current->isActive = 1;

    restoreInterruptsCustom(old_psr);
}

void blockMe() {
    check_kernel_mode();
    unsigned int old_psr = disableInterruptsCustom();
    Process *current = &processTable[currPID - 1];
    current->isActive = 0;
    current->isBlocked = 1;
    dispatcher();
    restoreInterruptsCustom(old_psr);
}

int unblockProc(int pid) {
    unsigned int old_psr = disableInterruptsCustom();
    check_kernel_mode();

    if (pid < 1 || pid >= MAXPROC || processTable[pid - 1].pid == -1) {
        return -2;  
    }

    Process *proc = &processTable[pid - 1];
    if (!proc->isBlocked) {
        return -2;
    }

    proc->isActive = 1;
    proc->isBlocked = 0;
    enqueue(proc);
    dispatcher();

    restoreInterruptsCustom(old_psr);
    return 0;
}

void enqueue(Process *proc) {
    int priority = proc->priority - 1;
    if (readyQueue[priority].tail == NULL) {
        readyQueue[priority].head = proc;
    } else {
        readyQueue[priority].tail->nextSibling = proc;
    }
    readyQueue[priority].tail = proc;
    proc->nextSibling = NULL;
}


Process *dequeue(int priority) {
    priority--;
    Process *process = readyQueue[priority].head;
    if (process != NULL) {
        readyQueue[priority].head = process->nextSibling;
        if (readyQueue[priority].head == NULL) {
            readyQueue[priority].tail = NULL;
        }
    }
    return process;
}

void printReadyQueue() {
    USLOSS_Console("---- Ready Queue Status ----\n");

    for (int i = 0; i < 6; i++) {  
        Process *current = readyQueue[i].head;

        if (current == NULL) {
            USLOSS_Console("Priority %d: [empty]\n", i + 1);
        } else {
            USLOSS_Console("Priority %d: ", i + 1);
            while (current != NULL) {
                USLOSS_Console("PID %d -> ", current->pid);
                current = current->nextSibling;
            }
            USLOSS_Console("NULL\n");
        }
    }

    USLOSS_Console("----------------------------\n");
}


Process *findNextProcess() {
    for (int i = 0; i < 6; i++) {
        if (readyQueue[i].head != NULL) {
            USLOSS_Console("REMOVING PRIORIITY LEVEL: %d\n", i + 1);
            return dequeue(i + 1);
        }
    }
    return NULL;
}


Process *findNextNonRunningProcess() {
    for (int i = 0; i < 6; i++) {  // Loop through priority levels from high to low
        Process *current = readyQueue[i].head;
        while (current != NULL) {
            if (!current->isActive && !current->hasQuit && !current->isBlocked) {
                return current;  // Found a process that is not running, not blocked, not quit
            }
            current = current->nextSibling;
        }
    }
    return NULL;
}

Process *findNextRunningProcess() {
    for (int i = 0; i < 6; i++) {  // Loop through priority levels from high to low
        Process *current = readyQueue[i].head;
        while (current != NULL) {
            if (current->isActive && !current->hasQuit) {
                return current;  // Found a running process that is not quit
            }
            current = current->nextSibling;
        }
    }
    return NULL;
}

void dispatcher() {
    check_kernel_mode();  // Ensure we're in kernel mode
    unsigned int old_psr = disableInterruptsCustom();

    static int lastSwitchTime = 0;  // Tracks the last context switch time
    int currentTimeValue = currentTime();

    // Get the current system time before switching processes
    //printReadyQueue();
    //dumpProcesses();

    if (initProc == 1) {
        initProc = 0;
        currPID = 1;
        USLOSS_ContextSwitch(NULL, &(processTable[0].context));
        lastSwitchTime = currentTime();
        restoreInterruptsCustom(old_psr);
        return;
    }

    Process *nextProcess = findNextNonRunningProcess();
    if (nextProcess == NULL) {
        USLOSS_Console("No non-running process found, looking for running process.\n");
        nextProcess = findNextRunningProcess();  // This function now checks for running processes
    }

    // If no process is found, halt the system (shouldn't happen)
    if (nextProcess == NULL) {
        USLOSS_Console("No runnable process found at any priority.\n");
        USLOSS_Halt(1);
    }

    Process *currentProcess = &processTable[currPID - 1];

    // If current process and next process are at the same priority level, implement time-slicing
    if (currentProcess->priority == nextProcess->priority) {
        int timeElapsed = currentTimeValue - lastSwitchTime;

        // Check if the current process has used up its time slice
        if (timeElapsed >= TIME_SLICE) {
            USLOSS_Console("Time slice expired for PID: %d. Requeuing.\n", currentProcess->pid);
            enqueue(currentProcess);  // Requeue the current process without dequeuing
            nextProcess = findNextNonRunningProcess();
            if (nextProcess == NULL) {
                nextProcess = findNextRunningProcess();  // Find another running process if no non-running ones
            }
        }
    }

    // Perform context switch only if next process is not the current one
    if (nextProcess->pid != currPID) {
        Process *previousProcess = currentProcess;
        currPID = nextProcess->pid;  // Set the current running process to the new one

        // Perform the context switch between processes
        USLOSS_Console("Context switching from PID %d to PID %d\n", previousProcess->pid, nextProcess->pid);
        USLOSS_ContextSwitch(&(previousProcess->context), &(nextProcess->context));

        // Update the last switch time after the context switch
        lastSwitchTime = currentTime();
    }

    restoreInterruptsCustom(old_psr);
}









void printProcess(int pid) {
    if (pid < 1 || pid >= MAXPROC || processTable[pid - 1].pid == -1) {
        USLOSS_Console("ERROR: Process with PID %d does not exist.\n", pid);
        return;
    }

    Process *proc = &processTable[pid - 1];

    USLOSS_Console("---- Process Information ----\n");
    USLOSS_Console("PID: %d\n", proc->pid);
    USLOSS_Console("Name: %s\n", proc->name);
    USLOSS_Console("Parent PID: %d\n", proc->ppid);
    USLOSS_Console("Priority: %d\n", proc->priority);
    USLOSS_Console("Status: %d\n", proc->status);
    USLOSS_Console("Active: %d\n", proc->isActive);
    USLOSS_Console("Has Quit: %d\n", proc->hasQuit);
    USLOSS_Console("Quit Status: %d\n", proc->quitStatus);
    USLOSS_Console("Stack Pointer: %p\n", proc->stack);
    USLOSS_Console("First Child PID: %d\n", (proc->firstChild != NULL) ? proc->firstChild->pid : -1);
    USLOSS_Console("Next Sibling PID: %d\n", (proc->nextSibling != NULL) ? proc->nextSibling->pid : -1);
    USLOSS_Console("Start Function Pointer: %p\n", proc->startFunc);
    USLOSS_Console("-----------------------------\n");
}




extern int getpid(void) {
    return currPID;
}

void dumpProcesses(void) {
    printf("PID\tName\t\tPriority\tStatus\tActive\tHasQuit\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].pid != -1) {
            printf("%d\t%s\t\t%d\t\t%d\t\t%d\t\t%d\n",
                   processTable[i].pid,
                   processTable[i].name,
                   processTable[i].priority,
                   processTable[i].status,
                   processTable[i].isActive,
                   processTable[i].hasQuit);
        }
    }
}

unsigned int disableInterruptsCustom(void) {
    unsigned int old_psr = USLOSS_PsrGet();  
    if ((old_psr & USLOSS_PSR_CURRENT_INT) != 0) {
        if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != USLOSS_DEV_OK) {
            USLOSS_Console("ERROR: Failed to disable interrupts.\n");
            USLOSS_Halt(1);
        }
    }
    return old_psr;  
}

void restoreInterruptsCustom(unsigned int old_psr) {
    if (USLOSS_PsrSet(old_psr) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Failed to restore PSR.\n");
        USLOSS_Halt(1);
    }
}
