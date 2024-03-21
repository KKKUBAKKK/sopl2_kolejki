#define _GNU_SOURCE
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "server_client.h"

#define MAX_NAME 20

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t work = 1;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void siginthandler(int sig)
{
    work = 0;
}

void server_work(mqd_t PID_S, mqd_t PID_D, mqd_t PID_M)
{
    clientMessage cmes;
    char name[MAX_NAME];
    int lengthRead = 0;
    mqd_t cmq;
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(clientMessage);

    if (sethandler(siginthandler, SIGINT))
        ERR("sethandler");

    while (work)
    {
        if ((lengthRead = TEMP_FAILURE_RETRY(mq_receive(PID_S, (char *)&cmes, sizeof(clientMessage), NULL))) < 0)
            if (errno != EAGAIN)
                ERR("mq_receive");
        if (lengthRead <= 0)
        {
            if ((lengthRead = TEMP_FAILURE_RETRY(mq_receive(PID_D, (char *)&cmes, sizeof(clientMessage), NULL))) < 0)
                if (errno != EAGAIN)
                    ERR("mq_receive");
        }
        if (lengthRead <= 0)
        {
            if ((lengthRead = TEMP_FAILURE_RETRY(mq_receive(PID_M, (char *)&cmes, sizeof(clientMessage), NULL))) < 0)
                if (errno != EAGAIN)
                    ERR("mq_receive");
        }

        if (lengthRead > 0)
        {
            snprintf(name, MAX_NAME,"/%d", cmes.clientPID);
            if ((cmq = TEMP_FAILURE_RETRY(mq_open(name, O_WRONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1)
                ERR("mq open cmq");

            if (TEMP_FAILURE_RETRY(mq_send(cmq, (const char *)&cmes, sizeof(clientMessage), 0)))
                ERR("mq_send");

            mq_close(cmq);
        }

        sleep(1);
    }
}

int main(int argc, char **argv)
{
    // Creating variables
    pid_t myPID = getpid();
    //pid_t myPID = 11111;
    char pids[20], pidd[20], pidm[20];
    mqd_t PID_s, PID_d, PID_m;
    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(clientMessage);

    // Creating message queues
    sprintf(pids, "/%d_s", myPID);
    if ((PID_s = TEMP_FAILURE_RETRY(mq_open(pids, O_RDONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq open PID_s");
    sprintf(pidd, "/%d_d", myPID);
    if ((PID_d = TEMP_FAILURE_RETRY(mq_open(pidd, O_RDONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq open PID_d");
        sprintf(pidm, "/%d_m", myPID);
    if ((PID_m = TEMP_FAILURE_RETRY(mq_open(pidm, O_RDONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq open PID_m");
    
    // Work
    printf("Server message queues:\n%s\n%s\n%s\n", pids, pidd, pidm);
    server_work(PID_s, PID_d, PID_m);

    // Closing and deleting queues
    printf("\nClosing server\n");
    mq_close(PID_s);
    mq_close(PID_d);
    mq_close(PID_m);
    if (mq_unlink(pids))
        ERR("mq unlink");
    if (mq_unlink(pidd))
        ERR("mq unlink");
    if (mq_unlink(pidm))
        ERR("mq unlink");

    return EXIT_SUCCESS;
}
