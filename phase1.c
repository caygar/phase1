/*
Name: Kavin Janarthanan
Partner: Cumhur Aygar

Project: Phase 1A
Description: Implementation of certain functions concerning 'Process Control' mechanism of OS kernel.
Course: CSC 452


*/
#include <stdio.h>
#include <stdlib.h>
#include <usloss.h>
#include "phase1.h"

int init(void *arg);
void sporkWrapper(void *arg);

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
} Process;

char initStack[USLOSS_MIN_STACK * 2];

Process processTable[MAXPROC];  
int nextPID = 2;                
int currPID = 1;                

// Helper function to check if we're in kernel mode
void check_kernel_mode() {
    int psr = USLOSS_PsrGet();
    if (!(psr & USLOSS_PSR_CURRENT_MODE)) {  // If not in kernel mode
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);  
    }
}


// Initialize the Phase 1 process table
void phase1_init(void) {
    for (int i = 0; i < MAXPROC; i++) {
        processTable[i].pid = -1;
        processTable[i].isActive = 0;
        processTable[i].priority = -1;
        processTable[i].status = 0;
        processTable[i].stack = NULL;
    }

    // Set up the init process in the process table at index 0
    processTable[0].pid = 1; 
    snprintf(processTable[0].name, MAXNAME, "%s", "Init");
    processTable[0].isActive = 1;
    processTable[0].priority = 6;
    processTable[0].stack = initStack;

    nextPID = 2;

    USLOSS_ContextInit(&(processTable[0].context), initStack, 2 * USLOSS_MIN_STACK, NULL, init);
}

// The init function creates the testcase_main process and switches to it
int init(void *arg) {
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();
    int pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");
    TEMP_switchTo(pid);

    USLOSS_Halt(0);  
    return 0;
}

// Create a new process using spork
int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
    check_kernel_mode();  
    int old_psr = USLOSS_PsrGet();  

    if (!(old_psr & USLOSS_PSR_CURRENT_MODE)) { 
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);  // Halt the simulation with error
    }

    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Failed to disable interrupts in spork()\n");
        USLOSS_Halt(1);
    }

    if (stackSize < USLOSS_MIN_STACK) {
        return -2;
    }

    if (priority < 1 || priority > 5 || startFunc == NULL || name == NULL || strlen(name) >= MAXNAME) {
        return -1;
    }

    int pid = nextPID;
    int slot = pid % MAXPROC;

    while (processTable[slot].isActive) {
        pid++;  // Increment PID and check the next available slot
        slot = pid % MAXPROC;

        // If we've wrapped around and the table is still full, return error
        if (pid - nextPID > MAXPROC) {
            return -1;  // No available slots
        }
    }

    // Allocate stack for the new process
    void *stack = malloc(stackSize);

    // Initialize the new process in the process table
    processTable[slot - 1].pid = pid;
    processTable[slot - 1].isActive = 1;
    processTable[slot - 1].priority = priority;
    snprintf(processTable[slot - 1].name, MAXNAME, "%s", name);
    processTable[slot - 1].stack = stack;
    processTable[slot - 1].startFunc = startFunc;
    processTable[slot - 1].arg = arg;

    USLOSS_ContextInit(&(processTable[slot - 1].context), stack, stackSize, NULL, sporkWrapper);


    nextPID++;

    // Restore the PSR
    if (USLOSS_PsrSet(old_psr) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Failed to restore PSR in spork()\n");
        USLOSS_Halt(1);
    }

    return processTable[slot - 1].pid;
}


// Wrapper for startFunc to ensure proper termination if startFunc returns
void sporkWrapper(void *arg) {

    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    int slot = (currPID % MAXPROC) - 1;
    int (*func)(void *) = processTable[slot].startFunc;
    void *funcArg = processTable[slot].arg;

    int status = func(funcArg);
    USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
    if (currPID == 1) {
        
        return; 
    } else {
        quit(status);
    }
}

// Quit function for Phase 1a (with manual process switching)
void quit_phase_1a(int status, int switchToPid) {
    // Check if the process is in kernel mode
    int psr = USLOSS_PsrGet();
    if (!(psr & USLOSS_PSR_CURRENT_MODE)) {  
        USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");
        USLOSS_Halt(1);
    }

    processTable[currPID - 1].isActive = 0;  
    processTable[currPID - 1].status = status;

    // Manually switch to the new process
    TEMP_switchTo(switchToPid);

    USLOSS_Halt(1);  
}



// General quit function (used later in Phase 1b)
void quit(int status) {
    processTable[currPID - 1].isActive = 0;  
    processTable[currPID - 1].status = status;

    USLOSS_Halt(1); 
}

// Returns current process ID
int getpid(void) {
    return currPID;
}

// Returns next process ID
int getNextpid(void) {
    return nextPID;
}

// Join process for parent process to wait for child process to complete, similar to WAIT() function in UNIX systems
int join(int *status) {
    check_kernel_mode();

    if (status == NULL) {
        return -3;
    }

    int foundChild = 0;
    int childPid = -2;  
    int highestPid = -1;

    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].isActive == 0 && processTable[i].status != -1) {
            if (processTable[i].pid > highestPid) {
                highestPid = processTable[i].pid;
                *status = processTable[i].status;
                childPid = processTable[i].pid;
                foundChild = 1;
            }
        }
    }

    if (foundChild) {
        for (int i = 0; i < MAXPROC; i++) {
            if (processTable[i].pid == childPid) {
                processTable[i].status = -1;
                break;
            }
        }
    }

    if (!foundChild) {
        return -2;
    }

    return childPid;
}

// Dump all processes
void dumpProcesses(void) {
    printf("PID\tName\t\tPriority\tStatus\tActive\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].pid != -1) {
            printf("%d\t%s\t\t%d\t\t%d\t\t%d\n", 
                processTable[i].pid,
                processTable[i].name,
                processTable[i].priority,
                processTable[i].status,
                processTable[i].isActive);
        }
    }
}

void TEMP_switchTo(int pid) {
    check_kernel_mode();

    int index = pid % MAXPROC - 1;

    if (index < 0 || index >= MAXPROC || processTable[index].isActive == 0) {
        dumpProcesses();  
        USLOSS_Halt(1);  
    }

    int oldpid = currPID;
    currPID = pid;  
    if (pid == 1) {
        USLOSS_ContextSwitch(NULL, &(processTable[index].context));
    } else {
        USLOSS_ContextSwitch(&(processTable[oldpid % MAXPROC - 1].context), &(processTable[index].context));
    }
}

