// Author: Adam Wilson
// Date: 10/2/2020

#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 

// declare state possibilities
//enum state { idle, want_in, in_cs };

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

struct sembuf p = { 0, -1, SEM_UNDO };
struct sembuf v = { 0, +1, SEM_UNDO };

void criticalSection(char*, int, int);

// child process determines whether or not string assigned to it is a palindrome,
// then writes the string to a corresponding output file while protecting the critical 
// section and outputting timings relating to the critical sections
int main(int argc, char* argv[]) {
    int i, j;
    struct timeval current;
    
    // read execl args into local variables
    i = atoi(argv[0]);
    int numStrings = atoi(argv[1]);

    // shared memory segment struct contains 2d string array, variables to control 
    // critical section, and original start time
    struct shmseg {
//        int turn;
//        enum state flag[20];
        char strings[numStrings][128];
        struct timeval start;
    };

    key_t semkey = ftok("master", 731);

    int semid = semget(semkey, 1, 0666 | IPC_CREAT);
    if (semid < 0)
    {
        perror("semget"); exit(11);
    }

    union semun u;
    u.val = 1;
    if (semctl(semid, 0, SETVAL, u) < 0)
    {
        perror("semctl"); exit(12);
    }

    // get same shmid as in master
    key_t shmkey = ftok("master", 137);
    int shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("palin: Error");
        exit(-1);
    }

    // attach struct pointer to shared memory segment
    struct shmseg * shmptr  = shmat(shmid, (void*)0, 0);
    if (shmptr == (void*)-1) {
        perror("palin: Error");
        exit(-1);
    }

    // puts strings[i] into a local variable for easier handling
    char* str = shmptr->strings[i];
    
    // sets pFlag to true if str is a palindrome and false if not
    int pFlag = 1;
    int left = 0;
    int right = strlen(str) - 1;
    while (right > left) {
        if (str[left++] != str[right--]) {
            pFlag = 0;
            break;
        }
    }

    // outputs current time to stderr
    gettimeofday(&current, NULL);
    fprintf(stderr, "Process %u began trying to enter critical section at %ul ns\n", getpid(),
        (current.tv_sec - shmptr->start.tv_sec) * 1000000 + current.tv_usec - shmptr->start.tv_usec);
    
    // makes sure only one process can enter its critical section at a time; ensures progress
    // and bounded wait
    if (semop(semid, &p, 1) < 0)
    {
        perror("semop p"); exit(13);
    }
    //do
    //{
    //    // raise this process's flag
    //    shmptr->flag[i] = want_in;
    //    // assign turn to local variable
    //    j = shmptr->turn;
    //    while (j != i)
    //        j = (shmptr->flag[j] != idle) ? shmptr->turn : (j + 1) % 20;
    //    // declare intention to enter critical section
    //    shmptr->flag[i] = in_cs;
    //    // check that no one else is in critical section
    //    for (j = 0; j < 20; j++)
    //        if ((j != i) && (shmptr->flag[j] == in_cs))
    //            break;
    //} while ((j < 20) || (shmptr->turn != i && shmptr->flag[shmptr->turn] != idle));
    
    // assign turn to self and enter critical section, outputting time to stderr
//    shmptr->turn = i;
    gettimeofday(&current, NULL);
    fprintf(stderr, "Process %u entered critical section at %ul ns\n", getpid(),
        (current.tv_sec - shmptr->start.tv_sec) * 1000000 + current.tv_usec - shmptr->start.tv_usec);
    
    // seed random number generator
    srand((unsigned int)current.tv_usec);

    // set random wait from 0-2 seconds, with nanosecond precision,
    // then sleep for that amount of time before writing to files
    long r = (rand() % 2000) * 1000000;
    struct timespec wait = { r / 1000000000, r % 1000000000};
    nanosleep(&wait, NULL);
    criticalSection(str, pFlag, i);

    // exit critical section, outputting exit time to stderr
    gettimeofday(&current, NULL);
    fprintf(stderr, "Process %u exited critical section at %ul ns\n", getpid(),
        (current.tv_sec - shmptr->start.tv_sec) * 1000000 + current.tv_usec - shmptr->start.tv_usec);
    if (semop(semid, &v, 1) < 0)
    {
        perror("semop p"); exit(14);
    }
    
    //j = (shmptr->turn + 1) % 20;
    //while (shmptr->flag[j] == idle)
    //    j = (j + 1) % 20;

    // assign turn to next waiting process; change own flag to idle
    //shmptr->turn = j;
    //shmptr->flag[i] = idle;

    // detach from shared memory
    if (shmdt(shmptr) == -1) {
        perror("palin: Error");
        exit(-1);
    }

	return 0;
}  

// handles the process's critical section, appending process info to 
// log file, and appending string to appropriate output file
void criticalSection(char* str, int pFlag, int index) {
    FILE* fp;
    FILE* fp2;

    // append process id, array index, and string to log file
    fp2 = fopen("output.log", "a");
    if (fp2 == NULL) {
        perror("palin: Error");
    }
    fprintf(fp2, "%u   %d   %s\n", getpid(), index, str);
    fclose(fp2);

    // append string to palin.out if it is a palindrome
    if (pFlag == 1) {
        fp = fopen("palin.out", "a");
        if (fp == NULL) {
            perror("palin: Error");
        }
        fprintf(fp, "%s\n", str);
    }

    // append string to nopalin.out if it is not a palindrome
    else {
        fp = fopen("nopalin.out", "a");
        if (fp == NULL) {
            perror("palin: Error");
        }
        fprintf(fp, "%s\n", str);
    }

    fclose(fp);
}
