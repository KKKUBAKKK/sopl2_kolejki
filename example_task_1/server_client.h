#ifndef SERVER_CLIENT

#define SERVER_CLIENT
#include <sys/wait.h>

    struct clientMessage
    {
        pid_t clientPID;
        int nums[2];
    } typedef clientMessage;
    
#endif