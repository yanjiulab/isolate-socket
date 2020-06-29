#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <wait.h>
#include <memory.h>
#include <syscall.h>
#include <errno.h>
#include "util.h"
#include "netns.h"

struct params {
    int fd[2];
    char **argv;
};

#define STACKSIZE (1024*1024)
static char cmd_stack[STACKSIZE];

void await_setup(int pipe)
{
    // We're done once we read something from the pipe.
    char buf[2];
    if (read(pipe, buf, 2) != 2)
        die("Failed to read from pipe: %m\n");
}

static int cmd_exec(void *arg)
{
    // Kill the cmd process if the isolate process dies.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL))
        die("cannot PR_SET_PDEATHSIG for child process: %m\n");

    struct params *params = (struct params*) arg;
    // Wait for 'setup done' signal from the main process.
    await_setup(params->fd[0]);

    // start a udp server
    int PORT = 8080;
    int MAXLINE = 1024;
    int sockfd; 
	char buffer[MAXLINE]; 
	char *hello = "Hello from server"; 
	struct sockaddr_in servaddr, cliaddr; 
	
	// Creating socket file descriptor 
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
	} 
	
	memset(&servaddr, 0, sizeof(servaddr)); 
	memset(&cliaddr, 0, sizeof(cliaddr)); 
	
	// Filling server information
	servaddr.sin_family = AF_INET; // IPv4
	servaddr.sin_addr.s_addr = inet_addr("10.0.0.2");
	servaddr.sin_port = htons(PORT);

	// Bind the socket with the server address 
	if ( bind(sockfd, (const struct sockaddr *)&servaddr, 
			sizeof(servaddr)) < 0 ) 
	{ 
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	}

	int n; 
	socklen_t len;
	printf("Server start at %s:%d... \n", inet_ntoa(servaddr.sin_addr), PORT);
	for (;;) {
		len = sizeof(cliaddr); //len is value/resuslt 

		n = recvfrom(sockfd, (char *)buffer, MAXLINE, 
					MSG_WAITALL, ( struct sockaddr *) &cliaddr, 
					&len); 
		buffer[n] = '\0'; 
		printf("Client[%s:%d] : %s\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), buffer); 
		sendto(sockfd, (const char *)hello, strlen(hello), 
			MSG_CONFIRM, (const struct sockaddr *) &cliaddr, 
				len); 
		printf("Hello message sent.\n"); 
	}
	
	return 0; 
}

static void prepare_netns(int cmd_pid)
{
    char *veth = "br-veth2";
    char *vpeer = "veth2";
    // char *veth_addr = "10.0.0.1";
    char *vpeer_addr = "10.0.0.2";
    char *netmask = "255.255.255.0";

    int sock_fd = create_socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);

    // ip link add veth1 type veth peer name br-veth1
    create_veth(sock_fd, veth, vpeer);

    // ip link set veth up 
    if_up0(veth);

    // add veth to ovs bridge
    char *base = "ovs-vsctl add-port br0";
    char *command = (char *) malloc(strlen(base) + strlen(veth));
    sprintf(command, "%s %s", base, veth);
    if (system(command) != 0) {
        printf("Command: %s executes error...", command);
    }
    free(command);

    int mynetns = get_netns_fd(getpid());
    int child_netns = get_netns_fd(cmd_pid);

    // ip link set vpeer netns 
    move_if_to_pid_netns(sock_fd, vpeer, child_netns);

    if (setns(child_netns, CLONE_NEWNET))
        die("Failed to setns for command at pid %d: %m\n", cmd_pid);

    if_up(vpeer, vpeer_addr, netmask);

    if (setns(mynetns, CLONE_NEWNET))
        die("Failed to restore previous net namespace: %m\n");

    close(sock_fd);
}

int main(int argc, char **argv)
{
    struct params params;
    memset(&params, 0, sizeof(struct params));

    // Create pipe to communicate between main and command process.
    if (pipe(params.fd) < 0)
        die("Failed to create pipe: %m");

    // Clone command process.
    int clone_flags = SIGCHLD | CLONE_NEWNET;
            
    int cmd_pid = clone(cmd_exec, cmd_stack + STACKSIZE, clone_flags, &params);

    if (cmd_pid < 0)
        die("Failed to clone: %m\n");

    // Get the writable end of the pipe.
    int pipe = params.fd[1];

    prepare_netns(cmd_pid);

    // Signal to the command process we're done with setup.
    if (write(pipe, "OK", 2) != 2)
        die("Failed to write to pipe: %m");
    if (close(pipe))
        die("Failed to close pipe: %m");

    if (waitpid(cmd_pid, NULL, 0) == -1)
        die("Failed to wait pid %d: %m\n", cmd_pid);

    return 0;
}
