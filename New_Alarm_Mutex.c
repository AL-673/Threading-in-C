#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#define NUM_ALARMS 3
#define MAX_INPUT_BUFFER 128
#define MAX_ALARM_MSG_LEN 128

typedef unsigned char BOOL;

#define TRUE 1
#define FALSE 0

typedef struct
{
    int alarm_id;                    // Alarm ID
    char message[MAX_ALARM_MSG_LEN]; // Alarm message
    time_t ring_time;                // alarm ring time set by the main thread
    int seconds;                     // alarm duration in seconds
    BOOL occupied;                   // flag to indicate whether the alarm is occupied to some thread or not
    BOOL close_requested;            // flag to signal the threads to stop/terminate gracefully
    pthread_t thread_id;             // thread id the alarm is occupied to
} alarm_t;

// alarm id counter
static int alarm_id_counter = 0;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;

// alarm list
alarm_t **alarms;

/**
 * Prints the help message showing the commands and meaning of the command 
 * parameteres.
 */
void print_help(char **argv)
{
    printf("%s usage\n\n", argv[0]);
    printf("%-16s%-64s\n", "help", "print this help text");
    printf("%-16s%-64s\n", "<t>", "t=alarm type(start, end)");
    printf("%-16s%-64s\n", "quit", "exit the program");
    printf("\n");
    printf("after entering start type alarm-\n");
    printf("%-16s%-64s\n", "<sec> <msg>", "sec=seconds, msg=alarm message(max 128 chars)");
    printf("\n");
    printf("after entering end type alarm-\n");
    printf("%-16s%-64s\n", "<id>", "id=alarm id");
    printf("\n");
}

// reads a null terminated line from stdin
void getline(char *line, int count)
{
    int cnt = 0;
    char ch;
    while (cnt < count - 1 && (ch = (char)getchar()) != EOF && ch != '\n')
    {
        line[cnt++] = ch;
    }
    line[cnt] = '\0';
}

// get the current time string without the trailing newline
char *get_time_str(time_t *t)
{
    char *data = asctime(localtime(t));
    size_t len = strlen(data);
    for (size_t i = len - 1; i >= 0; i++)
    {
        if (data[i] == '\n')
        {
            data[i] = '\0';
            break;
        }
    }
    return data;
}

/**
 * Alarm callback. This function will monitor the alarm
 * by provided id and execute it in time.
 */
void *alarm_callback(void *arg)
{
    int alarm_id = *((int *)arg);
    time_t now = time(NULL);
    BOOL alarm_found = FALSE;
    BOOL should_ring = FALSE;
    BOOL close_requested = FALSE;
    char message[MAX_ALARM_MSG_LEN];
    message[0] = '\0'; // null terminate for safety
    pthread_t tid = 0;
    int duration = 0;

    while (TRUE)
    {
        // refresh the state variables
        now = time(NULL);
        alarm_found = FALSE;
        should_ring = FALSE;
        close_requested = FALSE;

        pthread_mutex_lock(&alarm_mutex);
        // look for the alarm by id
        for (int i = 0; i < NUM_ALARMS; i++)
        {
            if (alarms[i]->alarm_id == alarm_id)
            {
                if (now >= alarms[i]->ring_time)
                {
                    should_ring = TRUE;          // schedule to ring
                    alarms[i]->occupied = FALSE; // mark the alarm as rang/unoccupied
                }

                if (alarms[i]->close_requested)
                {
                    close_requested = TRUE; // schedule to stop the thread
                }

                strcpy(message, alarms[i]->message);
                tid = alarms[i]->thread_id;
                duration = alarms[i]->seconds;
                alarm_found = TRUE;
                break;
            }
        }
        pthread_mutex_unlock(&alarm_mutex);

        if (close_requested)
        {
            printf("Alarm(%d) Ended at %s: %d %s\n", alarm_id, get_time_str(&now), duration, message);
            printf("alarm > "); // print the prompt just for clarity
            fflush(stdout);
            break; // simply close the loop
        }

        if (!alarm_found)
        {
            // exit the thread
            printf("Alarm(%d) Ended at %s: %d %s\n", alarm_id, get_time_str(&now), duration, message);
            printf("alarm > "); // print the prompt just for clarity
            fflush(stdout);
            break;
        }

        if (should_ring)
        {
            // ring the alarm and exit
            printf("Alarm(%d) Printed by Alarm Thread %ld at %s: %d %s\n", alarm_id, tid, get_time_str(&now), duration, message);
            printf("alarm > "); // print the prompt just for clarity
            fflush(stdout);
            break;
        }

        // sleep the thread for 1 sec otherwise
        sleep(1);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    // pre-allocate alarm memory
    alarms = (alarm_t **)malloc(NUM_ALARMS * sizeof(alarm_t *));
    if (alarms == NULL)
    {
        printf("[%d] Could not allocate memory!\n", __LINE__);
        exit(1);
    }
    for (int i = 0; i < NUM_ALARMS; i++)
    {
        alarm_t *a = (alarm_t *)malloc(sizeof(alarm_t));
        if (a == NULL)
        {
            printf("[%d] Could not allocate memory for alarm %d!\n", __LINE__, i);
            exit(1);
        }
        // mark the alarm as unoccupied
        a->occupied = FALSE;
        alarms[i] = a;
    }

    // temporary alarm data
    int tmp_alarm_duration, tmp_alarm_id;
    char tmp_alarm_msg[MAX_ALARM_MSG_LEN];
    char tmp_param_msg[MAX_ALARM_MSG_LEN + MAX_INPUT_BUFFER];

    // user input buffer
    char buffer[MAX_INPUT_BUFFER];

    // run the loop until user exits
    while (TRUE)
    {
        printf("alarm > ");
        getline(buffer, MAX_INPUT_BUFFER);
        if (strcmp(buffer, "quit") == 0)
        {
            printf("Good Bye\n");
            break;
        }
        else if (strcmp(buffer, "help") == 0)
        {
            print_help(argv);
        }
        else if (strcmp(buffer, "start") == 0)
        {
            // get parameters input
            printf("alarm > "); // show the prompt
            getline(tmp_param_msg, MAX_ALARM_MSG_LEN + MAX_INPUT_BUFFER);
            tmp_alarm_duration = -1;
            int check = sscanf(tmp_param_msg, "%d %[^\n]s", &tmp_alarm_duration, tmp_alarm_msg);
            if (check < 2 || tmp_alarm_duration < 0)
            {
                printf("alarm > invalid input! type help for commands\n");
                continue;
            }

            // find empty slot first
            BOOL assigned = FALSE;
            pthread_mutex_lock(&alarm_mutex);
            for (int i = 0; i < NUM_ALARMS; i++)
            {
                if (alarms[i]->occupied == FALSE)
                {
                    // occupy new thread to this alarm
                    alarms[i]->alarm_id = ++alarm_id_counter;
                    alarms[i]->close_requested = FALSE;
                    alarms[i]->seconds = tmp_alarm_duration;
                    time_t t = time(NULL);
                    time_t now = t;
                    t += tmp_alarm_duration;
                    alarms[i]->ring_time = t;
                    strcpy(alarms[i]->message, tmp_alarm_msg);

                    // print message
                    printf("alarm > Alarm(%d) Inserted by Main Thread Into Alarm List at %s: %d %s\n", alarms[i]->alarm_id, get_time_str(&now), tmp_alarm_duration, tmp_alarm_msg);

                    // create new thread
                    pthread_t tid;
                    int check = pthread_create(&tid, NULL, alarm_callback, &alarms[i]->alarm_id);
                    if (check != 0)
                    {
                        printf("[%d] Could not create new thread: %d!\n", __LINE__, check);
                        exit(-1);
                    }

                    // print message
                    printf("alarm > Created New Display Alarm thread %ld For Alarm(%d) at %s: %d %s\n", tid, alarms[i]->alarm_id, get_time_str(&now), tmp_alarm_duration, tmp_alarm_msg);

                    alarms[i]->thread_id = tid;
                    alarms[i]->occupied = TRUE;

                    // print message
                    printf("alarm > Alarm(%d) Assigned to Display Alarm Thread %ld at %s: %d %s\n", alarms[i]->alarm_id, tid, get_time_str(&now), tmp_alarm_duration, tmp_alarm_msg);

                    assigned = TRUE;

                    break;
                }
            }
            pthread_mutex_unlock(&alarm_mutex);

            // check if assigned
            if (!assigned)
            {
                printf("alarm > All(3) Alarm slots are occupied. Please wait to finish an alarm first.\n");
            }
        }
        else if (strcmp(buffer, "end") == 0)
        {
            // get user input for alarm id
            printf("alarm > "); // show the prompt
            getline(tmp_param_msg, MAX_ALARM_MSG_LEN + MAX_INPUT_BUFFER);
            tmp_alarm_id = -1;
            int check = sscanf(tmp_param_msg, "%d", &tmp_alarm_id);
            if (check < 1 || tmp_alarm_id < 0)
            {
                printf("alarm > invalid input! type help for commands\n");
                continue;
            }

            // if possible schedule to stop the alarm
            BOOL found = FALSE;
            pthread_mutex_lock(&alarm_mutex);
            for (int i = 0; i < NUM_ALARMS; i++)
            {
                if (alarms[i]->alarm_id == tmp_alarm_id)
                {
                    alarms[i]->close_requested = TRUE;
                    found = TRUE;
                    printf("alarm > Alarm(%d) is Scheduled to End.\n", alarms[i]->alarm_id);
                }
            }
            pthread_mutex_unlock(&alarm_mutex);

            if (!found)
            {
                printf("alarm > Alarm(%d) is Not Found!\n", tmp_alarm_id);
            }
        }
        else
        {
            printf("alarm > Unknown/Invalid Command (do not include trailing spaces in commad).\n");
        }
    }

    /* wait for occupied threads to finish up */
    pthread_mutex_lock(&alarm_mutex);
    // request all alarm threads to close/stop
    for (int i = 0; i < NUM_ALARMS; i++)
    {
        alarms[i]->close_requested = TRUE;
    }
    pthread_mutex_unlock(&alarm_mutex);
    // now join and wait
    for (int i = 0; i < NUM_ALARMS; i++)
    {
        if (alarms[i]->occupied)
        {
            pthread_join(alarms[i]->thread_id, NULL);
        }
    }

    // free up memory before exiting
    for (int i = 0; i < NUM_ALARMS; i++)
    {
        free(alarms[i]);
    }
    free(alarms);

    return 0;
}