// Author: Adam Wilson
// Date: 10/2/2020

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/sem.h>
#include <sys/types.h>

// nMax is global so interrupt handler can affect it
int nMax;

// declares possible flag states
//enum state { idle, want_in, in_cs };
//
//union semun {
//    int val;
//    struct semid_ds* buf;
//    unsigned short* array;
//};
//
//struct sembuf p = { 0, -1, SEM_UNDO };
//struct sembuf v = { 0, +1, SEM_UNDO };

// kills all processes in this process group, which is ignored by master,
// then zeros out nMax so no new processes are spawned
static void interruptHandler(int s) {
    fprintf(stderr, "Interrupt recieved.");
    signal(SIGQUIT, SIG_IGN);
    kill(-getpid(), SIGQUIT);
    nMax = 0;
}

// sets up sigaction for SIGALRM
static int setupAlarmInterrupt(void) {
    struct sigaction sigAlrmAct;
    sigAlrmAct.sa_handler = interruptHandler;
    sigAlrmAct.sa_flags = 0;
    sigemptyset(&sigAlrmAct.sa_mask);
    return (sigaction(SIGALRM, &sigAlrmAct, NULL));
}

// sets up sigaction for SIGINT, using same handler as SIGALRM to avoid conflict
static int setupSIGINT(void) {
    struct sigaction sigIntAct;
    sigIntAct.sa_handler = interruptHandler;
    sigIntAct.sa_flags = 0;
    sigemptyset(&sigIntAct.sa_mask);
    return (sigaction(SIGINT, &sigIntAct, NULL));
}

// sets ups itimer with value of tMax and interval of 0
static int setupitimer(int t) {
    struct itimerval value = { {0, 0}, {t, 0} };
    return (setitimer(ITIMER_REAL, &value, NULL));
}

// reads an input file of strings into shared memory, then spawns up to nMax child processes, sMax at a time,
// by fork-execing the palin executable to process each of those strings; then adds final time to logfile; stops
// execution if tMax is reached; ensures there are no shared memory leaks or zombie processes
int main(int argc, char* argv[]) {
    int i, opt, tMax, sMax, status;
    int numStrings = 0, currentProcesses = 0;
    char c;
    pid_t  pid;
    key_t shmkey;
    key_t semkey;
    struct timeval current, start;
    FILE* fp;
    FILE* fp2;

    // set default parameters
    nMax = 4;
    sMax = 2;
    tMax = 100;

    // initialize start time
    gettimeofday(&start, NULL);

    // parses command line arguments
    while ((opt = getopt(argc, argv, "hn:s:t:")) != -1) {
        switch (opt) {
            case 'h':
                printf("\n\n\n--- master/palin Help Page ---\n\n\
                    master reads a file of strings, and for each string creates a child process (palin) to determine\n\
                    whether the string is a palindrome; all palindromes are written to palin.out, and all\n\
                    non-palindromes to nopalin.out\n\n\
                      Invocation:\nmaster -h\nmaster [-n x] [-s x] [-t time] infile\n  Options:\n\
                    -h Opens master/palin Help Page.\n\
                    -n x Sets the maximum total number of child processes master will ever create. (Default 4, Maximum 20)\n\
                    -s x Sets the number of children allowed to exist in the system at the same time. (Default 2)\n\
                    -t time Sets the time in seconds after which the process will terminate, even if it has not finished. (Default 100)\n\
                    infile Input file containing strings to be tested.");
                return 0;
            // nMax equals the arg after -n, or 20 if the arg is greater than 20
            case 'n':
                nMax = (atoi(optarg) < 20) ? atoi(optarg) : 20;
                break;
            case 's':
                sMax = atoi(optarg);
                break;
            case 't':
                tMax = atoi(optarg);
                break;
        }
    }

    // exits program if unable to open input file indicated in command line, or if the number of cmd args is incorrect
    if (optind = argc - 1) {
        fp = fopen(argv[optind], "r");
        if (fp == NULL) {
            perror("master: Error");
            exit(-1);
        }
    }
    else {
        fprintf(stderr, "master: Error: wrong number of command line arguments");
        exit(-1);
    }

    // sets up timer, and SIGALRM and SIGINT handlers
    if (setupitimer(tMax) == -1) {
        perror("master: Error");
        exit(-1);
    }
    if (setupAlarmInterrupt() == -1) {
        perror("master: Error");
        exit(-1);
    }
    if (setupSIGINT() == -1) {
        perror("master: Error");
        exit(-1);
    }

    // gets number of strings in input file by counting number of newlines, then rewinds filepointer
    for (c = getc(fp); c != EOF; c = getc(fp)){
        if (c == '\n')
            numStrings++;
    } 
    rewind(fp);
    
    // shared memory segment struct contains 2d string array, variables to control 
    // critical section, and original start time
    struct shmseg {
       // int turn;
       // enum state flag[20];
        char strings[numStrings][128];
        struct timeval startTime;
    };

    // create shared memory segment the same size as struct shmseg and get its shmid
    shmkey = ftok("master", 137);
    //semkey = ftok("master", 731);

    //int semid = semget(KEY, 1, 0666 | IPC_CREAT);
    //if (semid < 0)
    //{
    //    perror("semget"); exit(11);
    //}

    //union semun u;
    //u.val = 1;
    //if (semctl(semid, 0, SETVAL, u) < 0)
    //{
    //    perror("semctl"); exit(12);
    //}

    int shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("master: Error");
        exit(-1);
    }

    // attach struct pointer to shared memory segment
    struct shmseg* shmptr = shmat(shmid, (void*)0, 0);
    if (shmptr == (void*)-1) {
        perror("master: Error");
        exit(-1);
    }

    // initialize turn to 0 and all flags to idle
    shmptr->turn = 0;
    for (i = 0; i < 20; i++) {
        shmptr->flag[i] = idle;
    }
    
    shmptr->startTime = start;

    // reads input file, string-by-string, into shared memory, adding null terminator to each string
    i = 0;
    while (fgets(shmptr->strings[i], 128, fp)) {
        shmptr->strings[i][strlen(shmptr->strings[i]) - 1] = '\0';
        i++;
    }
    fclose(fp);

    // forks child processes until the cumulative total reaches either numStrings or nMax
    for (i = 0; i < numStrings && i < nMax; i++) {
        pid = fork();
        currentProcesses++;

        if (pid == -1) {
            perror("master: Error");
        }

        // in forked child, converts i and numStrings to char arrays so they can be used as 
        // args in execl() call to palin executable
        else if (pid == 0) {
            char num[10], index[10];
            sprintf(index, "%d", i);
            sprintf(num, "%d", numStrings);
            execl("palin", index, num, (char*)NULL);
            exit(0);
        }

        // in parent process, waits for exit signals until the number of active child processes is below sMax
        else {
            while (currentProcesses >= sMax) {
                if (wait(&status) > 0) {
                    if (WIFEXITED(status) && !WEXITSTATUS(status)) ;
                    else if (WIFEXITED(status) && WEXITSTATUS(status)) {
                        if (WEXITSTATUS(status) == 127) perror("master: Error");
                        else perror("master: Error");
                    }
                    else perror("master: Error");
                }
                currentProcesses--;
            }
        }
    }

    // waits for exit code from all remaining children
    for (i = 0; i < currentProcesses; i++) {
        if (wait(&status) > 0) {
            if (WIFEXITED(status) && !WEXITSTATUS(status));
            else if (WIFEXITED(status) && WEXITSTATUS(status)) {
                if (WEXITSTATUS(status) == 127) perror("master: Error");
                else perror("master: Error");
            }
            else perror("master: Error");
        }
    }

    // detaches strings array from shared memory, then destroys shared memory segment
    if (shmdt(shmptr) == -1) {
        perror("master: Error");
        exit(-1);
    }
    if (shmctl(shmid, IPC_RMID, 0) == -1) {
        perror("master: Error");
        exit(-1);
    }

    // appends final time to logfile
    fp2 = fopen("output.log", "a");
    if (fp2 == NULL) {
        perror("master: Error");
        exit(-1);
    }
    gettimeofday(&current, NULL);
    fprintf(fp2, "\n\tFinal time: %.5f s\n\n", ((current.tv_sec - start.tv_sec) * 1000000 + current.tv_usec - start.tv_usec) / 1000000.);
    fclose(fp2);

    return 0;
}