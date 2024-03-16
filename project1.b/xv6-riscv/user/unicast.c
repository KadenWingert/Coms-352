#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX_NUM_RECEIVERS 10
#define MAX_MSG_SIZE 256

// Define the structure for the message
struct msg_t
{
    int flags[MAX_NUM_RECEIVERS];
    char content[MAX_MSG_SIZE];
    int receiverId;
};

// Function to handle panic situations
void panic(char *s)
{
    fprintf(2, "%s\n", s);
    exit(1);
}

// Function to fork a new process
int fork1(void)
{
    int pid;
    pid = fork();
    if (pid == -1)
        panic("fork");
    return pid;
}

// Function to create a pipe
void pipe1(int fd[2])
{
    int rc = pipe(fd); // rc is the pipe
    if (rc < 0)
    {
        panic("Fail to create a pipe.");
    }
}

int main(int argc, char *argv[])
{
    // Checks to see if command-line arguments are provided correctly
    if (argc < 4)
    { // This is the proper format of the command line arguments
        panic("Usage: unicast <num_of_receivers> <receiver_id> <msg_to_send>");
    }

    // Parse command-line arguments
    // The first word is unicast, the second is the number of recievers
    int numReceiver = atoi(argv[1]);
    // The thrid argument is the recieving child
    int receiverId = atoi(argv[2]);

    // Create pipes for communication
    int channelToReceivers[2], channelFromReceivers[2];
    pipe1(channelToReceivers);
    pipe1(channelFromReceivers);

    // Create child processes
    for (int i = 0; i < numReceiver; i++)
    {
        int retFork = fork1();
        if (retFork == 0)
        {
            // Child process code
            int myId = i;
            printf("Child %d: start!\n", myId);

            // Read the message from the pipe
            struct msg_t msg;
            read(channelToReceivers[0], (void *)&msg, sizeof(struct msg_t));

            printf("Child %d: get msg (%s) to Child %d\n", myId, msg.content, receiverId);

            // Indicates that the child whom the message was intended for has been foundf
            if (msg.receiverId == myId)
            {
                printf("Child %d: the msg is for me.\n", myId);
                // Write acknowledgment to the parent
                write(channelFromReceivers[1], "received!", 10);
                printf("Child %d acknowledges to Parent: received!\n", myId);
            }
            else
            {
                printf("Child %d: the msg is not for me.\n", myId);
                // Write the message back to the pipe
                write(channelToReceivers[1], &msg, sizeof(msg));
                printf("Child %d: write the message back to pipe.\n", myId);
            }

            exit(0);
        }
        else
        {
            // Parent process code
            printf("Parent: creates child process with id: %d\n", i);
        }
        sleep(1);
    }

    // Prepare the message to be sent
    struct msg_t msg;
    for (int i = 0; i < numReceiver; i++)
    {
        msg.flags[i] = 1;
    }

    strcpy(msg.content, argv[3]);
    msg.receiverId = receiverId;

    // Send the message to the specified child
    write(channelToReceivers[1], &msg, sizeof(struct msg_t));
    printf("Parent sends to Child %d: %s\n", receiverId, msg.content);

    char recvBuf[sizeof(struct msg_t)];
    read(channelFromReceivers[0], &recvBuf, sizeof(struct msg_t));
    printf("Parent receives: %s\n", recvBuf);

    // Exit the parent process
    exit(0);
}
