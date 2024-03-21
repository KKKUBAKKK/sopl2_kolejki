#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MIN_POS -1000
#define MAX_POS 1000
#define MAX_MSGS 10
#define MQ_DR_NAME_LENGTH 20

volatile sig_atomic_t work = 1;

typedef struct driver_message
{
    int pid;
    int distance;
} driver_message;

typedef struct driver_position
{
    int x;
    int y;
} driver_position;

typedef struct uber_task
{
    driver_position pickup;
    driver_position target;
} uber_task;

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigalrm_handler(int sig)
{
    (void) sig;
    work = 0;
}

void usage(const char* name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: 1 <= N - number of drivers\n");
    fprintf(stderr, "T: 5 <= T - simulation duration\n");
    exit(EXIT_FAILURE);
}

int get_rand_pos(int min, int max)
{
    return min + rand() % (max - min);
}

int calculate_city_metric(driver_position origin, driver_position pickup, driver_position target)
{
    int dx = abs(target.x - pickup.x);
    int dy = abs(target.y - pickup.y);
    int distance = dx + dy;

    dx = abs(pickup.x - origin.x);
    dy = abs(pickup.y - origin.y);
    distance += dx + dy;

    return distance;
}

/*int calculate_regular_metric(driver_position origin, driver_position pickup, driver_position target)
{
    int dx = abs(target.x - pickup.x);
    int dy = abs(target.y - pickup.y);
    int distance = (int) sqrt(dx * dx + dy * dy);

    dx = abs(pickup.x - origin.x);
    dy = abs(pickup.y - origin.y);
    distance += (int) sqrt(dx * dx + dy * dy);

    return distance;
}*/

int calculate_sleep_time(driver_position origin, driver_position pickup, driver_position target)
{
    int sleep_time = 2 * calculate_city_metric(origin, pickup, target);
    //sleep_time += calculate_regular_metric(origin, pickup, target);

    return sleep_time / 1000;
}

uber_task generate_task()
{
    uber_task ut;
    ut.pickup.x = get_rand_pos(MIN_POS, MAX_POS);
    ut.pickup.y = get_rand_pos(MIN_POS, MAX_POS);
    ut.target.x = get_rand_pos(MIN_POS, MAX_POS);
    ut.target.y = get_rand_pos(MIN_POS, MAX_POS);

    return ut;
}

void driver_work(char *mq_name, char *mq_driver)
{
    sethandler(SIG_IGN, SIGALRM);

    int sleep_time;
    driver_message mes;
    mes.pid = getpid();
    struct timespec t = {0, 0};
    srand(getpid());
    unsigned int priority;
    driver_position pos;
    pos.x = get_rand_pos(MIN_POS, MAX_POS);
    pos.y = get_rand_pos(MIN_POS, MAX_POS);
    mqd_t uber_tasks, report_back;
    uber_task ut;
    struct mq_attr attr;
    attr.mq_maxmsg = MAX_MSGS;
    attr.mq_msgsize = sizeof(uber_task);

    printf("Driver %d starting position: x = %d; y = %d\n", getpid(), pos.x, pos.y);

    if ((uber_tasks = TEMP_FAILURE_RETRY(mq_open(mq_name, O_RDONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq_open");
    attr.mq_msgsize = sizeof(driver_message);
    if ((report_back = TEMP_FAILURE_RETRY(mq_open(mq_driver, O_WRONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq_open");
    while (1)
    {
        if (TEMP_FAILURE_RETRY(mq_receive(uber_tasks, (char *) &ut, sizeof(uber_task), &priority)) < 1)
                ERR("mq_receive");
        if (priority == 1)
            break;
        sleep_time = calculate_sleep_time(pos, ut.pickup, ut.target);
        mes.distance = calculate_city_metric(pos, ut.pickup, ut.target);
        t.tv_sec = sleep_time;
        pos = ut.target;
        if (TEMP_FAILURE_RETRY(mq_send(report_back, (const char *)&mes, sizeof(driver_message), 0)))
            ERR("mq_send");
        // printf("Driver %d moved to target: x = %d; y = %d\n", getpid(), pos.x, pos.y);
        nanosleep(&t, &t);
    }
    mq_close(report_back);
    mq_close(uber_tasks);
    printf("Driver ending work\n");
}

void parent_work(int N, int T, char *mq_name, char *mq_drivers[])
{
    srand(getpid());
    struct timespec t = {0, 0};
    uber_task ut;
    mqd_t uber_tasks, drivers[N];
    driver_message mes;
    struct mq_attr attr;
    attr.mq_maxmsg = MAX_MSGS;
    attr.mq_msgsize = sizeof(uber_task);

    if ((uber_tasks = TEMP_FAILURE_RETRY(mq_open(mq_name, O_WRONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq_open");
    attr.mq_msgsize = sizeof(driver_message);
    for (int i = 0; i < N; i++)
    {
        if ((drivers[i] = TEMP_FAILURE_RETRY(mq_open(mq_drivers[i], O_RDONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
            ERR("mq_open");
    }

    sethandler(sigalrm_handler, SIGALRM);
    alarm(T);

    while (work)
    {
        ut = generate_task();
        if (TEMP_FAILURE_RETRY(mq_send(uber_tasks, (const char *)&ut, sizeof(uber_task), 0)))
        {
            if (errno != EAGAIN)
                ERR("mq_send");
            fprintf(stderr, "Task rejected\n");
        }
        
        t.tv_sec = get_rand_pos(500, 2000) / 1000;
        nanosleep(&t, &t);

        for (int i = 0; i < N; i++)
        {
            if (work == 0)
                break;
            if (TEMP_FAILURE_RETRY(mq_receive(drivers[i], (char *) &mes, sizeof(driver_message), NULL)) < 1)
            {
                if (errno != EAGAIN)
                    ERR("mq_send");
                continue;
            }
            printf("Driver %d drove a distance of %d\n", mes.pid, mes.distance);
        }
    }
    mq_close(uber_tasks);
    attr.mq_msgsize = sizeof(uber_task);
    if ((uber_tasks = TEMP_FAILURE_RETRY(mq_open(mq_name, O_WRONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq_open");
    for (int i = 0; i < N; i++)
    {
        if (TEMP_FAILURE_RETRY(mq_send(uber_tasks, (const char *)&ut, sizeof(uber_task), 1)))
            ERR("mq_send");
    }
    for (int i = 0; i < N; i++)
    {
        mq_close(drivers[i]);
        if (mq_unlink(mq_drivers[i]))
            ERR("mq unlink");
    }
    mq_close(uber_tasks);
    if (mq_unlink(mq_name))
            ERR("mq unlink");

    printf("Uber ending work\n");
}

void create_drivers(int N, char *mq_name, char *mq_driver[])
{
    while (N-- > 0)
    {
        switch (fork())
        {
        case 0:
            driver_work(mq_name, mq_driver[N]);
            exit(EXIT_SUCCESS);
        
        default:
            break;
        }
    }
}

int main(int argc, char** argv)
{
    // Check the command-line arguments
    if (argc != 3)
        usage(argv[0]);
    int N = atoi(argv[1]);
    int T = atoi(argv[2]);
    if (N < 1 || T < 5)
        usage(argv[0]);

    // Create message queue name
    char *mq_name = "/uber_tasks";

    // Create message queue names for all drivers
    char *mq_drivers[N];
    for (int i = N - 1; i >= 0; i--)
    {
        mq_drivers[i] = calloc(MQ_DR_NAME_LENGTH, sizeof(char));
        snprintf(mq_drivers[i], MQ_DR_NAME_LENGTH, "/driver%d", i + 1);
    }

    // Do work
    create_drivers(N, mq_name, mq_drivers);
    parent_work(N, T, mq_name, mq_drivers);

    // Wait for all the child processes to end
    while(wait(NULL) > 0);

    for (int i = 0; i < N; i++)
        free(mq_drivers[i]);

    return EXIT_SUCCESS;
}