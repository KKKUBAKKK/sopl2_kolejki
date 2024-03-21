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

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void client_work(char *mqServer)
{
    // Creating variables
    mqd_t cmq, smq;
    pid_t myPID = getpid();
    char name[20];
    int end = 1;
    clientMessage cmes;
    cmes.clientPID = myPID;
    struct mq_attr attr;
    attr.mq_maxmsg = 5;
    attr.mq_msgsize = sizeof(clientMessage);

    // Creating client message queue
    sprintf(name, "/%d", myPID);
    if ((cmq = TEMP_FAILURE_RETRY(mq_open(name, O_RDONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq open cmq");

    // Open the mq provided by the server
    if ((smq = TEMP_FAILURE_RETRY(mq_open(mqServer, O_WRONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq open smq");

    // Work
    printf("Client message queue:\n%s\n", name);
    printf("Enter two integers:\n");
    end = scanf("%d %d", &cmes.nums[0], &cmes.nums[1]);
    if (TEMP_FAILURE_RETRY(mq_send(smq, (const char *)&cmes, sizeof(clientMessage), 0)))
            ERR("mq_send");
    while (end != EOF)
    {
        if (TEMP_FAILURE_RETRY(mq_receive(cmq, (char *)&cmes, sizeof(clientMessage), NULL)) < 1)
            ERR("mq_receive");
        printf("%d received: %d, %d\n", cmes.clientPID, cmes.nums[0], cmes.nums[1]);

        printf("Enter two integers:\n");
        end = scanf("%d %d", &cmes.nums[0], &cmes.nums[1]);
        if (TEMP_FAILURE_RETRY(mq_send(smq, (const char *)&cmes, sizeof(clientMessage), 0)))
                ERR("mq_send");
    }
    printf("Client closing\n");

    // Closing and deleting queue
    mq_close(smq);
    mq_close(cmq);
    if (mq_unlink(name))
        ERR("mq unlink");
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./client /mq_server\n");
        exit(EXIT_FAILURE);
    }
    char *mq_server = argv[1];
    //char *mq_server = "/11111_s";

    client_work(mq_server);

    return EXIT_SUCCESS;
}