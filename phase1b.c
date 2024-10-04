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
    int hasQuit;
    int quitStatus;
    int ppid;  
    struct Process *nextSibling;
    struct Process *firstChild;
} Process;

char initStack[USLOSS_MIN_STACK * 2];

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
    }

    processTable[0].pid = 1;
    snprintf(processTable[0].name, MAXNAME, "%s", "Init");
    processTable[0].isActive = 1;
    processTable[0].priority = 6;
    processTable[0].stack = initStack;

    nextPID = 2;

    USLOSS_ContextInit(&(processTable[0].context), initStack, 2 * USLOSS_MIN_STACK, NULL, init);
}

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

int spork(char *name, int (*startFunc)(void *), void *arg, int stackSize, int priority) {
    check_kernel_mode();  
    int old_psr = USLOSS_PsrGet();  

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
        pid++;
        slot = pid % MAXPROC;
        if (pid - nextPID > MAXPROC) {
            return -1;
        }
    }

    void *stack = malloc(stackSize);
    processTable[slot].pid = pid;
    processTable[slot].isActive = 1;
    processTable[slot].priority = priority;
    snprintf(processTable[slot].name, MAXNAME, "%s", name);
    processTable[slot].stack = stack;
    processTable[slot].startFunc = startFunc;
    processTable[slot].arg = arg;
    processTable[slot].ppid = currPID;

    if (processTable[currPID - 1].firstChild == NULL) {
        processTable[currPID - 1].firstChild = &processTable[slot];
    } else {
        Process *child = processTable[currPID - 1].firstChild;
        while (child->nextSibling != NULL) {
            child = child->nextSibling;
        }
        child->nextSibling = &processTable[slot];
    }

    USLOSS_ContextInit(&(processTable[slot].context), stack, stackSize, NULL, sporkWrapper);
    nextPID++;

    if (USLOSS_PsrSet(old_psr) != USLOSS_DEV_OK) {
        USLOSS_Console("ERROR: Failed to restore PSR in spork()\n");
        USLOSS_Halt(1);
    }

    return processTable[slot].pid;
}

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

void quit_phase_1a(int status, int switchToPid) {
    int psr = USLOSS_PsrGet();
    if (!(psr & USLOSS_PSR_CURRENT_MODE)) {
        USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");
        USLOSS_Halt(1);
    }

    processTable[currPID - 1].isActive = 0;
    processTable[currPID - 1].status = status;
    processTable[currPID - 1].hasQuit = 1;
    processTable[currPID - 1].quitStatus = status;

    TEMP_switchTo(switchToPid);

    USLOSS_Halt(1);
}

void quit(int status) {
    processTable[currPID - 1].isActive = 0;
    processTable[currPID - 1].status = status;
    processTable[currPID - 1].hasQuit = 1;
    processTable[currPID - 1].quitStatus = status;

    if (processTable[currPID - 1].ppid != -1) {
        Process *parent = &processTable[processTable[currPID - 1].ppid - 1];
        parent->hasQuit = 0;
    }

    USLOSS_Halt(1);
}

int getpid(void) {
    return currPID;
}

int join(int *status) {
    check_kernel_mode();

    if (status == NULL) {
        return -3;
    }

    int foundChild = 0;
    int childPid = -2;
    int highestPid = -1;

    Process *child = processTable[currPID - 1].firstChild;

    while (child != NULL) {
        if (child->hasQuit) {
            *status = child->quitStatus;
            foundChild = 1;
            childPid = child->pid;
            break;
        }
        child = child->nextSibling;
    }

    if (!foundChild) {
        return -2;
    }

    processTable[childPid - 1].status = -1;

    return childPid;
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
