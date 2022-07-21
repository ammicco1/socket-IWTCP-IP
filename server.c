/********************************************************************************/
/*																				*/
/* Program:			Server that offers a text echo service via TCP on			*/
/*					IPv4 or IPv6												*/
/*																				*/
/* Method: 			Repeadetly accept a TCP connection, echo lines of text		*/ 
/*					until the client close the connecron, and go on wait 		*/
/*					for the next connection										*/
/*																				*/
/* Use: 			server [-p port]											*/
/*																				*/
/*					Where port is a TCP port number or name						*/
/*																				*/
/* Author: 			Barry Shein, bsx@TheWorld.com, 3/1/2013						*/
/*																				*/
/********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

static char *prog; 				/* ptr to program name (for message) 	*/

/* This is arbitrary but should be unprivileged (> 1024) */ 
#define DEFAULT_PORT "9000"		/* must match client default port 		*/ 

/* Define process exit codes */ 
#define EX_OK 		0	/* Normal termination 		*/
#define EX_ARGFALL	1	/* Incorrect arguments 		*/
#define EX_SYSERR	2	/* Error in system call 	*/ 
#define EX_NOMEM	3	/* Cannot allocate memory 	*/

#define MAXLEN 255

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/* Server structure used to pass information internally */ 

typedef struct{
	int 	sock;			/* socket descriptor 				*/ 
	char 	*port_name;		/* ptr to name of port being used 	*/
	int 	port_number;	/* integer value for port 			*/ 
	FILE 	*ferr;			/* stdio handle for error messages 	*/
}_Server, *Server;

/* Log - display error or information message for user */

static void Log(Server srv, char *fmt, ...){
    va_list ap;

    va_start(ap, fmt);
    (void) vfprintf(srv -> ferr, fmt, ap);
    va_end(ap);
}

/* Fatal - display a fatal error message for user and exit */

static void Fatal(Server srv, int exval, char *fmt, ...){
    va_list ap;

    va_start(ap, fmt);
    (void) vfprintf(srv -> ferr, fmt, ap);
    va_end(ap);

    exit(exval);
}

/* newServer - create a new server object */ 

static Server newServer(void){
	Server srv; 

	/* allocate memory for new server, exit on error */ 
	if((srv = (Server) calloc(1, sizeof(*srv))) == NULL){
		(void) fprintf(stderr, "%s", strerror(errno));
		exit(EX_NOMEM);
	}else{
		srv -> ferr = stderr; /* initialize log output */ 
		return srv; 
	}
}

/* freeServer - free memory associated with an istance of a server struct */ 

static void freeServer(Server srv){
	if(srv -> port_name != NULL){
		free(srv -> port_name);
	}

	free(srv);
}

/* initServer - initialize an istance of a server struct */ 

static Server initServer(char *port){
	Server srv;
	char *protocol = "tcp";
	struct protoent *pp;
	struct servent *sport;
	char *ep;
	extern const struct in6_addr in6addr_any;
	struct sockaddr_storage sa; 
	int sopt = 0;
	extern int errno;

	srv = newServer(); /* exit on failure */ 
	srv -> port_name = strdup(port); /* save port name they passed */

	/* look up protocol number for "tcp" */
	if((pp = getprotobyname(protocol)) == NULL){
		Fatal(srv, EX_ARGFALL, ANSI_COLOR_RED "initServer: %s\n" ANSI_COLOR_RESET, strerror(errno));
	}

	/* first see if port number is a string of digits, such as "9000"  */
	/* and then see if it is a name sach as "echo" (see /etc/services) */

	if(((srv -> port_number = strtol(srv -> port_name, &ep, 0)) > 0) && (*ep == '\0')){
		srv -> port_number = htons(srv -> port_number);
	}else if((sport = getservbyname(srv -> port_name, protocol)) == NULL){
		Fatal(srv, EX_ARGFALL, ANSI_COLOR_RED "initServer: bad port %s\n" ANSI_COLOR_RESET, srv -> port_name);
	}else{
		srv -> port_number = sport -> s_port; /* success */
	}

	/* get a new IPv4 or IPv6 socket and prepare it for bind() */
	(void) memset(&sa, 0, sizeof(sa));

	if((srv -> sock = socket(AF_INET6, SOCK_STREAM, pp -> p_proto)) < 0){
		if(errno == EAFNOSUPPORT){ /* no IPv6 on this system, use IPv4 */
			if((srv -> sock = socket(AF_INET, SOCK_STREAM, pp -> p_proto)) < 0){
				Fatal(srv, EX_SYSERR, ANSI_COLOR_RED "initServer: socket: %s\n" ANSI_COLOR_RESET, strerror(errno));
			}else{
				struct sockaddr_in *sa4 = (struct sockaddr_in *) &sa;
				sa4 -> sin_family = AF_INET;
				sa4 -> sin_port = srv -> port_number;
				sa4 -> sin_addr.s_addr = INADDR_ANY;
			}
		}
	}else{ /* IPv6 supported */
		struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *) &sa;
		
		/* set the socket option IPV6_V6ONLY to zero (off) so we   */
		/* will listen for both IPv6 and IPv4 incoming connections */ 
		if(setsockopt(srv -> sock, IPPROTO_IPV6, IPV6_V6ONLY, &sopt, sizeof(sopt)) < 0){
			Fatal(srv, EX_SYSERR, ANSI_COLOR_RED "initServer: setsockopt: %s\n" ANSI_COLOR_RESET, strerror(errno));
		}

		sa6 -> sin6_family = AF_INET6;
		sa6 -> sin6_port = srv -> port_number;
		sa6 -> sin6_addr = in6addr_any; /* listen to any iface & addr */				
	}

	/* bind the new socket to the service */
	if(bind(srv -> sock, (const struct sockaddr *) &sa, sizeof(sa)) < 0){
		Fatal(srv, EX_SYSERR, ANSI_COLOR_RED "initServer: bind: %s\n" ANSI_COLOR_RESET, strerror(errno));
	}

	/* set the maximum number of waiting incoming connections */
	if(listen(srv -> sock, SOMAXCONN) < 0){
		Fatal(srv, EX_SYSERR, ANSI_COLOR_RED "initServer: listen: %s\n" ANSI_COLOR_RESET, strerror(errno));
	}

	return srv;
}

/* runServer - run the server & iteratly accept incoming connestions */

static void runServer(Server srv){
	int counter = 0; 

	while(1){ /* iterate forever (until user abort process) */
		int s; 
		char sendb[MAXLEN], recvb[MAXLEN];
		int bytes, byter;

		memset(sendb, 0, MAXLEN);
		memset(recvb, 0, MAXLEN);

		/* sockaddr_storage is large enough to hold IPv6 or IPv4 informations */
		struct sockaddr_storage addr; 
		socklen_t addrlen = sizeof(addr);
		struct sockaddr *sap = (struct sockaddr *) &addr;

		/* accept will block for a new incoming connection */
		(void) memset(&addr, 0, sizeof(addr));

		if((s = accept(srv -> sock, sap, &addrlen)) >= 0){
			int pid;

			counter++;

			pid = fork();

			if(pid == 0){
				close(srv -> sock);

				int my_conn_sd = counter;

				char host[NI_MAXHOST];
				char service[NI_MAXSERV];

				/* get information about new client */
				if(getnameinfo(sap, addrlen, host, sizeof(host), service, sizeof(service), 0) != 0){
					Log(srv, "getnameinfo: %s\n", strerror(errno));
					(void) shutdown(s, SHUT_RDWR);
					continue;
				}

				Log(srv, ANSI_COLOR_GREEN "accept connection n. %d: host = %s port = %s\n" ANSI_COLOR_RESET, my_conn_sd, host, service);

				while(1){
					if((byter = read(s, recvb, MAXLEN)) <= 0){
						Log(srv, ANSI_COLOR_RED "read: error read buffer\n" ANSI_COLOR_RESET);
						break;
					}else{ /* a valid connection has been accepted */ 
						Log(srv, ANSI_COLOR_GREEN "client: " ANSI_COLOR_RESET "%s\n", recvb);

						if(byter < 16){
							sprintf(sendb, "ok", byter, recvb);
							if((bytes = write(s, sendb, MAXLEN)) <= 0){
								Log(srv, ANSI_COLOR_RED "read: error read buffer\n" ANSI_COLOR_RESET);
								(void) shutdown(s, SHUT_RDWR);
								break; 
							}
						}else{
							sprintf(sendb, "noname", byter, recvb);
							if((bytes = write(s, sendb, MAXLEN)) <= 0){
								Log(srv, ANSI_COLOR_RED "read: error read buffer\n" ANSI_COLOR_RESET);
								(void) shutdown(s, SHUT_RDWR);
								continue; 
							}
						}
					}
				}
			
				while(recvb){
					if((byter = read(s, recvb, MAXLEN)) <= 0){
						Log(srv, ANSI_COLOR_RED "read: error read buffer\n" ANSI_COLOR_RESET);
						break;
					}else{ /* a valid connection has been accepted */ 
						Log(srv, ANSI_COLOR_GREEN "client: " ANSI_COLOR_RESET "%s\n", recvb);

						sprintf(sendb, "recived: %d bytes: %s", byter, recvb);
				
						if((bytes = write(s, sendb, MAXLEN)) <= 0){
							Log(srv, ANSI_COLOR_RED "read: error read buffer\n" ANSI_COLOR_RESET);
							(void) shutdown(s, SHUT_RDWR);
							continue; 
						}
					}
				}

				Log(srv, ANSI_COLOR_CYAN "client closed connection\n" ANSI_COLOR_RESET);
				break;
				
				if(shutdown(s, SHUT_RDWR) != 0){
					Log(srv, ANSI_COLOR_RED "%s, shutdown error\n" ANSI_COLOR_RESET, strerror(errno));
				}
			}else if(pid > 0){
				close(s);
			}else{
				Log(srv, ANSI_COLOR_RED "%s, fork error\n" ANSI_COLOR_RESET, strerror(errno));
			}
		}
	}
}

/* doneServer - user aborted process, so close server socket and log */ 

static void doneServer(Server srv){
	if(shutdown(srv -> sock, SHUT_RDWR) != 0){
		Log(srv, ANSI_COLOR_RED "%s, shutdown error\n" ANSI_COLOR_RESET, strerror(errno));
	}

	freeServer(srv);
	Log(srv, "\n%s: shutdown \n\n", prog);
}

/* handle server shutdown when various signal occur */ 

static jmp_buf sigenv;
static void onSignal(int signo){
	longjmp(sigenv, signo); /* send back signal num if anyone cares */ 
}

/* Usage - helpful command line message */

static void Usage(void){
    (void) printf("Usage: %s [-p tcp_port]\n", prog);
    exit(EX_OK);
}

/* main - main program: parse arguments and start the server */ 

int main(int argc, char **argv){
	Server srv;
	char *port = DEFAULT_PORT; /* default protocol port to use */ 
	int c; 

	prog = strrchr(*argv, '/') ? strrchr(*argv, '/') + 1 : *argv;

	/* parse arguments */

	while((c = getopt(argc, argv, "hp:")) != EOF){
		switch(c){
			case 'p': port = optarg; break;
			case 'h': default: Usage();
		}
	}	

	srv = initServer(port); /* exit on error */

	if(setjmp(sigenv) > 0){
		doneServer(srv); /* to here to signal */
		exit(EX_OK);
	}else{
		signal(SIGHUP, onSignal);
		signal(SIGINT, onSignal);
		signal(SIGTERM, onSignal);
	}

	Log(srv, "\n%s: Initialized, waiting for incoming connections\n\n", prog);

	runServer(srv);
	
	return(EX_OK);
}
