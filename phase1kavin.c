#include <stdio.h>
#include <stdlib.h>
#include <usloss.h>
#include "phase1.h"

int init(void *arg);
void sporkWrapper(void *arg);

// Process structure definition
typedef struct Process {
    int pid;
    char name[MAXNAME];
    USLOSS_Context context;
    int status;
    int priority;
    int isActive;
    void *stack;
    int (*startFunc)(void *);  // Function pointer to the process's main function
    void *arg;
} Process;

char initStack[USLOSS_MIN_STACK * 2];

Process processTable[MAXPROC];  // Process table
int nextPID = 2;                // Next available PID
int currPID = 1;                // Currently running process PID

void check_kernel_mode() {
    int psr = USLOSS_PsrGet();
    if (!(psr & USLOSS_PSR_CURRENT_MODE)) {  // If not in kernel mode
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);  // Halt the simulation with error
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
    processTable[0].pid = 1;  // PID 1 for the init process
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

    USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
    USLOSS_Halt(0);  // Halt normally if testcase_main returns
    return 0;
}

// Create a new process using spork
// Create a new process using spork
int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
    check_kernel_mode();  // Ensure kernel mode
    int old_psr = USLOSS_PsrGet();  // Save PSR to restore later

    // Check if the system is in kernel mode
    if (!(old_psr & USLOSS_PSR_CURRENT_MODE)) {  // If the mode bit is not set (i.e., in user mode)
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);  // Halt the simulation with error
    }

    // Disable interrupts
    if (USLOSS_PsrSet(old_psr & ~USLOSS_PSR_CURRENT_INT) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Failed to disable interrupts in spork()\n");
        USLOSS_Halt(1);
    }

    // Error check: Stack size must be at least USLOSS_MIN_STACK
    if (stackSize < USLOSS_MIN_STACK) {
        return -2;
    }

    // Error check: Ensure valid priority range (1-5), valid function pointer, and name length
    if (priority < 1 || priority > 5 || startFunc == NULL || name == NULL || strlen(name) >= MAXNAME) {
        return -1;
    }

    // Try to find an available slot in the process table using pid % MAXPROC rule
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

    //USLOSS_Console("Process %s (PID %d, slot %d) initialized successfully.\n", name, processTable[slot - 1].pid, slot);

    // Increment nextPID for the next process
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
    // Enable interrupts before calling startFunc
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    // Get the current process slot based on currPID
    int slot = (currPID % MAXPROC) - 1;
    int (*func)(void *) = processTable[slot].startFunc;
    void *funcArg = processTable[slot].arg;

    int status = func(funcArg);

    if (currPID == 1) {
        return;  // Normal halt for testcase_main
    } else {
        quit(status);
    }
}

// Quit function for Phase 1a (with manual process switching)
void quit_phase_1a(int status, int switchToPid) {
    processTable[currPID - 1].isActive = 0;  // Adjust index based on PID
    processTable[currPID - 1].status = status;

    TEMP_switchTo(switchToPid);

    USLOSS_Halt(1);  // Halt if quit_phase_1a is called without switching
}

// General quit function (used later in Phase 1b)
void quit(int status) {
    processTable[currPID - 1].isActive = 0;  // Adjust index based on PID
    processTable[currPID - 1].status = status;

    USLOSS_Halt(1);  // Halt system (this should be handled differently in Phase 1b)
}

// Return the current process ID
int getpid(void) {
    return currPID;
}

int getNextpid(void) {
    return nextPID;
}

// Join with a child process
// Join with a child process
int join(int *status) {
    check_kernel_mode();

    // Check if the status pointer is NULL, return -3 if invalid
    if (status == NULL) {
        return -3;
    }

    int foundChild = 0;
    int childPid = -2;  // Default return value if no children are found
    int highestPid = -1;

    // Iterate over the process table to find the dead child with the highest PID
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

    // Mark that we've found a child and can join
    if (foundChild) {
        for (int i = 0; i < MAXPROC; i++) {
            if (processTable[i].pid == childPid) {
                // Reset the child's status so it can't be joined again
                processTable[i].status = -1;
                break;
            }
        }
    }

    // If no children found, return -2, otherwise return the PID of the joined child
    if (!foundChild) {
        return -2;
    }

    return childPid;
}


// Dump all processes in the process table
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

// Temporary manual process switch function
void TEMP_switchTo(int pid) {
    check_kernel_mode();

    int index = pid % MAXPROC - 1;

    if (index < 0 || index >= MAXPROC || processTable[index].isActive == 0) {
        dumpProcesses();  // Dump process table for debugging
        USLOSS_Halt(1);   // Halt the simulation with an error
    }

    int oldpid = currPID;
    currPID = pid;  // Update the currently running process to the new PID
    if (pid == 1) {
        USLOSS_ContextSwitch(NULL, &(processTable[index].context));
    } else {
        USLOSS_ContextSwitch(&(processTable[oldpid % MAXPROC - 1].context), &(processTable[index].context));
    }
}

// Cleanup function to free allocated stacks
void cleanup(void) {
    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].stack != NULL) {
            free(processTable[i].stack);
            processTable[i].stack = NULL;
        }
    }
}

void printProcess(int pid) {
    int slot = pid % MAXPROC - 1;

    if (processTable[slot].pid == -1) {
        return;
    }

    printf("Process Information for PID %d (Slot %d):\n", processTable[slot].pid, slot);
    printf("Name: %s\n", processTable[slot].name);
    printf("Priority: %d\n", processTable[slot].priority);
    printf("Status: %d\n", processTable[slot].status);
    printf("isActive: %d\n", processTable[slot].isActive);
    printf("Stack Pointer: %p\n", processTable[slot].stack);
    printf("Function Pointer (startFunc): %p\n", processTable[slot].startFunc);
    printf("Argument Pointer (arg): %p\n", processTable[slot].arg);
}
