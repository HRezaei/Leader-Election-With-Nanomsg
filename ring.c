#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>
#include <limits.h>  // for INT_MAX
#include <unistd.h>
#include <termios.h>



char *elected = "0";
int participated = 0;
char *identifier = "1";
int neighbour_socket = 0;
int prev_socket = 0;
char election_msg[50] = "election:";
char coordinator_msg[50] = "coordinator:";

int cast_int(const char *arg) {
	char *p;
	int num;

	errno = 0;
	long conv = strtol(arg, &p, 10);

	// Check for errors: e.g., the string does not represent an integer
	// or the integer is larger than int
	if (errno != 0 || *p != '\0' || conv > INT_MAX) {
	    printf("Invalid integer %s", arg);
	    return -1;
	} else {
	    // No error
	    num = conv; 
	    return num;
	}
}

int connect_previous (const char *url)
{
  prev_socket = nn_socket(AF_SP, NN_PULL);
  assert (prev_socket >= 0);
  assert (nn_bind (prev_socket, url) >= 0);
}

int connect_neighbour (const char *url)
{
  //int sz_msg = strlen (msg) + 1; // '\0' too
  neighbour_socket = nn_socket (AF_SP, NN_PUSH);
  assert (neighbour_socket >= 0);
  assert (nn_connect (neighbour_socket, url) >= 0);
}


int send_neighbour(const char *msg) {
	printf ("SENDING \"%s\" to neighbour...", msg);
	fflush(stdout);
	int sz_msg = strlen (msg) + 1;
	int bytes = nn_send (neighbour_socket, msg, sz_msg, 0);
  	assert (bytes == sz_msg);
	printf ("Sent \"%d\" bytes\n", bytes);
	fflush(stdout);
}


int start_election(){
	participated = 1;
	send_neighbour(election_msg);
	
}

int process_election_msg(char *msg) {
	char msg_copy[50];
	strcpy(msg_copy, msg);
	char *received_id;
	strtok(msg_copy, ":");
	received_id = strtok(NULL, ":");
	int candid = cast_int(received_id);
	int my_id = cast_int(identifier);
	if( candid > my_id ) {

		send_neighbour(msg);
		participated = 1;
	}
	else if(candid == my_id) {
		elected = identifier;
		participated = 0;
		send_neighbour(coordinator_msg);
	}
	else if(participated != 1) {
		send_neighbour(election_msg);
		participated = 1;
	}
}

int process_coordinator_msg(char *msg) {
	char msg_copy[50];
	strcpy(msg_copy, msg);
	char *received_id = strtok(msg_copy, ":");
	received_id = strtok(NULL, ":");

	participated = 0;
	elected = received_id;
	printf ("elected=%s\n", elected);

	if(strcmp(identifier, received_id) != 0) {
		send_neighbour(msg);
	}
	return 1;
}


int listen_incommings(int mode) {

	while (1) {
		char *buf = NULL;
		int bytes = nn_recv (prev_socket, &buf, NN_MSG , NN_DONTWAIT);

		if (bytes >= 0) {
		
			printf ("RECEIVED \"%s\"\n", buf);
			char *copy;
			//strcpy(copy, buf)
			if(strncmp(buf, "election:", 8)==0) {
				process_election_msg(buf);
			}
			else if(strncmp(buf, "coordinator:", 12)==0) {
				process_coordinator_msg(buf);
			}
		}
		char *line = NULL;
	  	ssize_t bufsize = 0; // have getline allocate a buffer for us

		if(mode) {
			printf("\nEnter=Continue [start]=New election [modeoff]=Disable interactive mode [q]=Exit\n");
			getline(&line, &bufsize, stdin);

			if(strncmp(line, "start", 5)==0) {
				start_election();				
			}
			else if(strncmp(line, "modeoff", 7)==0) {
				mode = 0;				
			}
			else if(strncmp(line, "q", 1)==0) {
				nn_shutdown (neighbour_socket, 0);
				nn_shutdown (prev_socket, 0);
				return 1;
			}
		}
	}
}




/**
ring 2 ipc:///tmp/node1to2.ipc ipc:///tmp/node2to3.ipc start
**/
int main (const int argc, char **argv)
{
	int mode = 0;
	printf ("elected=%s\n", elected);
  if (argc < 4) {
	printf ("Usage: ring identifier previous_url neighbour_url\n");

	return 1;
  }
  else {
	identifier = argv[1];
	strcat(election_msg, identifier);
	strcat(coordinator_msg, identifier);

	connect_previous(argv[2]);
	connect_neighbour(argv[3]);
	printf ("my identifier=%s\n", identifier);
	if(strcmp(identifier, "3")==0) {	
		start_election();
	}
	if(argc==5 && strncmp(argv[4], "wait",4)==0) {
		mode = 1;
	}
	listen_incommings(mode);

      return 1;
    }
}
