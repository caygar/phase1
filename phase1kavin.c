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
    // Get the current PSR value
    int psr = USLOSS_PsrGet();
    
    // Check if the system is in kernel mode
    if (!(psr & USLOSS_PSR_CURRENT_MODE)) {  // If the mode bit is not set (i.e., in user mode)
        USLOSS_Console("ERROR: Function called from user mode! Halting...\n");
        USLOSS_Halt(1);  // Halt the simulation with error
    }
}

// Initialize the Phase 1 process table
void phase1_init(void) {
    // Initialize process table
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
    TEMP_switchTo(pid);

    //USLOSS_Console("testcase_main has returned. Halting simulation.\n");
    USLOSS_Halt(0);  // Halt normally if testcase_main returns
    return 0;
}


// Create a new process using spork
int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
    check_kernel_mode();  // Ensure kernel mode
    int old_psr = USLOSS_PsrGet();  // Save PSR to restore later

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
    processTable[slot - 1 ].isActive = 1;
    processTable[slot - 1].priority = priority;
    snprintf(processTable[slot -1].name, MAXNAME, "%s", name);
    processTable[slot - 1].stack = stack;

    processTable[slot - 1].startFunc = startFunc;
    processTable[slot - 1].arg = arg;

    USLOSS_ContextInit(&(processTable[slot - 1].context), stack, stackSize, NULL, sporkWrapper);

    USLOSS_Console("Process %s (PID %d, slot %d) initialized successfully.\n", name, processTable[slot - 1].pid, slot);

    // Increment nextPID for the next process
    nextPID = pid + 1;

    // Restore the PSR
    if (USLOSS_PsrSet(old_psr) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Failed to restore PSR in spork()\n");
        USLOSS_Halt(1);
    }
    

    // Process creation successful, return the PID of the child
    return processTable[slot - 1].pid;
}



// Wrapper for startFunc to ensure proper termination if startFunc returns
void sporkWrapper(void *arg) {
    USLOSS_Console("Entering sporkWrapper with PID: %d...\n", getpid());
    // Enable interrupts before calling startFunc
    USLOSS_PsrSet(USLOSS_PsrGet() | USLOSS_PSR_CURRENT_INT);

    int slot = getpid() - 1;
    int (*func)(void *) = processTable[slot ].startFunc;
    void *funcArg = processTable[slot ].arg;

    // Debugging: Print function pointer and argument values
    USLOSS_Console("sporkWrapper: Function pointer for PID %d: %p\n", currPID, func);
    USLOSS_Console("sporkWrapper: Argument for PID %d: %p (value: %s)\n", currPID, funcArg, (char *)funcArg);

    // Call the start function and print when function execution begins
    USLOSS_Console("sporkWrapper: Calling the function...\n");
    int status = func(funcArg);
    USLOSS_Console("sporkWrapper: Function executed, returned status: %d\n", status);

    // If testcase_main returns, halt the simulation normally
    if (currPID != processTable[0].pid) {
        USLOSS_Console("sporkWrapper: ERROR: Unexpected function return. Halting simulation.\n");
        USLOSS_Halt(1);  // Non-zero error code if user function returns
    }

    // Normal quit if testcase_main or any child calls quit
    USLOSS_Console("sporkWrapper: Quitting with status %d\n", status);
    quit(status);  
}



// Quit function for Phase 1a (with manual process switching)
void quit_phase_1a(int status, int switchToPid) {
    processTable[currPID - 1].isActive = 0;  // Adjust index based on PID
    processTable[currPID - 1].status = status;

    printf("Process %d quitting with status %d. Switching to PID %d\n", currPID, status, switchToPid);

    // Manually switch to the new process
    TEMP_switchTo(switchToPid);

    USLOSS_Halt(1);  // Halt if quit_phase_1a is called without switching
}

// General quit function (used later in Phase 1b)
void quit(int status) {
    processTable[currPID - 1].isActive = 0;  // Adjust index based on PID
    processTable[currPID - 1].status = status;

    printf("Process %d quitting with status %d\n", currPID, status);

    USLOSS_Halt(1);  // Halt system (this should be handled differently in Phase 1b)
}

// Return the current process ID
int getpid(void) {
    return currPID;
}

// Join with a child process
int join(int *status) {
    for (int i = 0; i < MAXPROC; i++) {
        if (processTable[i].isActive == 0 && processTable[i].status != -1) {
            *status = processTable[i].status;
            printf("Joined with process %d, status: %d\n", processTable[i].pid, *status);
            return processTable[i].pid;
        }
    }
    return -1;  // No children to join
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

    int index = pid - 1;  // Adjust the index based on PID (PID 1 is at index 0)

   // USLOSS_Console("PID: %d CURR PID: %d\n", pid, currPID);

    // Check if the process to switch to is valid and active
    if (index < 0 || index >= MAXPROC || processTable[index].isActive == 0) {
        //USLOSS_Console("ERROR: Attempted to switch to inactive or invalid process %d\n", pid);
        dumpProcesses();  // Dump process table for debugging
        USLOSS_Halt(1);   // Halt the simulation with an error
    }

    // Perform context switch
    int oldpid = currPID;
    if (pid == 1){
        printf("entering name: %s\n", processTable[index].name);
        USLOSS_ContextSwitch(NULL, &(processTable[index].context));
    }else{
        USLOSS_ContextSwitch(&(processTable[oldpid - 1].context), &(processTable[index].context));
    }
    

    // Update the currently running process
    currPID = pid;

    //USLOSS_Console("Successfully switched from process %d to process %d.\n", oldpid, pid);
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
        USLOSS_Console("Process with PID %d does not exist.\n", pid);
        return;
    }

    USLOSS_Console("Process Information for PID %d (Slot %d):\n", processTable[slot].pid, slot);
    USLOSS_Console("Name: %s\n", processTable[slot].name);
    USLOSS_Console("Priority: %d\n", processTable[slot].priority);
    USLOSS_Console("Status: %d\n", processTable[slot].status);
    USLOSS_Console("isActive: %d\n", processTable[slot].isActive);
    USLOSS_Console("Stack Pointer: %p\n", processTable[slot].stack);
    USLOSS_Console("Function Pointer (startFunc): %p\n", processTable[slot].startFunc);
    USLOSS_Console("Argument Pointer (arg): %p\n", processTable[slot].arg);
}
