#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <nanomsg/nn.h>
#include <nanomsg/survey.h>
#include <nanomsg/pubsub.h>


#define SERVER "server"
#define CLIENT "client"
#define URL_TEMPLATE   "ipc:///tmp/bully-node-%d.ipc"
#define COORDINATOR_URL_TEMPLATE   "ipc:///tmp/coordinator-node-%d.ipc"

int start_election ();

int identifier = 0;
int sock_downside = -1;
int coordinator_socket = -1;
int announce_socket = -1;
int election_socket = -1;
char node_url[50];
char coordination_url[50];
int elected = 0;
int verbose = 0;

void make_url (int i, char templa[50], char url[50])
{
	snprintf(url, 50, templa, i);
}

int announce_coordinator(int newly=0) {
	if(verbose) printf("Announcing my coordination\n");
	if(announce_socket<0)	{
		announce_socket = nn_socket (AF_SP, NN_PUB);
		assert (announce_socket >= 0);
		assert (nn_bind (announce_socket, coordination_url) >= 0);
	}
	  
	char d[50];
	sprintf(d, "Coordinator:%d", identifier);
	int sz_d = strlen(d) + 1; // '\0' too
	if(newly) {	
		printf ("Node-%d: PUBLISHING NEW COORDINATOR %s\n", identifier, d);
	}else {
		printf("Node-%d: PUBLISHING %s\n", identifier, d);
	}
	int bytes = nn_send (announce_socket, d, sz_d, 0);
	assert (bytes == sz_d);
}


int listen_coordinator() {
	if(verbose) printf("Listening upward\n");
	if(coordinator_socket<0) {	
		coordinator_socket = nn_socket (AF_SP, NN_SUB);
		assert (coordinator_socket >= 0);
		assert (nn_setsockopt (coordinator_socket, NN_SUB, NN_SUB_SUBSCRIBE, "", 0) >= 0);
		char url[50];
		for(int i=identifier+1; i<20; i++) {
			snprintf(url, 50, COORDINATOR_URL_TEMPLATE, i);
			if(verbose) printf("subscribed to %s for coordination messages\n",  url);
			assert (nn_connect (coordinator_socket, url) >= 0);
		}
	}

	time_t start = time(0);
	double elapsed_time=0;
	int duration = 3;
	if(elected!=identifier) {
		duration=20;
	}
	while (elapsed_time<duration)
	{
		char *buf = NULL;
		int bytes = nn_recv (coordinator_socket, &buf, NN_MSG, NN_DONTWAIT);
		if (bytes >= 0) {
			printf ("Node-%d RECEIVED %s\n", identifier, buf);
			strtok(buf, ":");
			char *coordinator = strtok(NULL, ":");
			nn_freemsg (buf);
			int previous_leader = elected;
			elected = atoi(coordinator);
			if(previous_leader != elected) {
				printf("elected=%d\n", elected);
			}
			return elected;
		}
		elapsed_time = difftime( time(0), start);			
	}
	if(verbose) printf("elapsed time:%f\n", elapsed_time);
	return 0;
}

int start_election ()
{
	if(verbose) printf("Started election\n");
	if(election_socket<0) {
		election_socket = nn_socket (AF_SP, NN_SURVEYOR);
		assert (election_socket >= 0);
		//change timeout NN_SURVEYOR_DEADLINE
		assert (nn_bind (election_socket, node_url) >= 0);
		int u = 3000;
		nn_setsockopt(election_socket, NN_SURVEYOR, NN_SURVEYOR_DEADLINE, &u, sizeof(u));
		 
		sleep(1); // wait for connections
	}
	
	char election_msg[50];
	sprintf(election_msg, "election:%d", identifier);
  int sz_d = strlen(election_msg) + 1; // '\0' too
  printf ("Node-%d: SENDING %s TO HIGHER NODES\n", identifier, election_msg);
  int bytes = nn_send (election_socket, election_msg, sz_d, 0);
  assert (bytes == sz_d);
  while (1)
    {
      char *buf = NULL;
      int bytes = nn_recv (election_socket, &buf, NN_MSG, 0);

      if (nn_errno() == ETIMEDOUT) {
		printf ("Node-%d: DID NOT RECEIVE ANY ANSWER FROM HIGHER NODES AND TIMED OUT\n", identifier);
		elected = identifier;		
		announce_coordinator(1);
		return identifier;
	}
      if (bytes >= 0)
      {
        printf ("Node-%d: RECEIVED \"%s\" FROM HIGHER NODES\n", identifier, buf);
        nn_freemsg (buf);
	int new_coordinator = listen_coordinator();
	if(!new_coordinator) {
		return start_election();
	}
	return new_coordinator;
      }
    }
	
  return 0;
}

int connect_downside() {
	sock_downside = nn_socket (AF_SP, NN_RESPONDENT);
	assert (sock_downside >= 0);
	int i = 0;
	char url[50], number[50];
	for(i=identifier-1; i>0; i--) {
		//make_url(i, URL_TEMPLATE, url);
		snprintf(url, 50, URL_TEMPLATE, i);
		assert (nn_connect (sock_downside, url) >= 0);
	}
}

void listen_downside() {
	if(verbose) printf("Listening downside\n");
      char *buf = NULL;
	//survey socket
      int bytes = nn_recv (sock_downside, &buf, NN_MSG, NN_DONTWAIT);
      if (bytes >= 0)
        {
          printf ("Node-%d: RECEIVED \"%s\" \n", identifier, buf);
          nn_freemsg (buf);
          char d[7] = "answer";
          int sz_d = strlen(d) + 1; // '\0' too
          printf ("Node-%d: SENDING answer RESPONSE\n", identifier);
          int bytes = nn_send (sock_downside, d, sz_d, 0);//@todo: resolve issue: this line will send answer to all downside sokets, even if they had not start any election
          assert (bytes == sz_d);
		start_election();
        }
}



/**
./bully 4 
*/
int main (const int argc, const char **argv)
{
  if (argc < 2) {
	printf ("Usage: bully <IDENTIFIER> <ARG> ...\n");    
	return 0;
  }else
    {
	identifier = atoi(argv[1]);
	snprintf(node_url, 50, URL_TEMPLATE, identifier);
	snprintf(coordination_url, 50, COORDINATOR_URL_TEMPLATE, identifier);
	printf("Node URL is:%s\n", node_url);
	printf("Coordination URL is:%s\n", coordination_url);
	connect_downside();
	int new_leader = start_election();
	while(1) {	
		listen_downside();
		new_leader = listen_coordinator();
		if(!new_leader & elected!=identifier) {
			printf("COORDINATOR NOT RESPONSING, STARTING NEW ELECTION...\n");
			start_election();
		}
		if(elected==identifier) {
			announce_coordinator(0);
		}
	}
	
      return 1;
    }
}
