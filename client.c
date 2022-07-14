/********************************************************************************/
/*																				*/
/* Program:			Client to test an example of echo server					*/
/*																				*/
/* Method: 			From a TCP connection to the echo server and repeatedly		*/
/*					read a line of text, send the text to server and recive		*/
/*					the same text back from the server							*/
/*																				*/
/* Use: 			client [-p port] host										*/
/*																				*/
/*					Where port is a TCP port number or name, and host is 		*/
/*					the name or IP address of the server's host					*/
/*																				*/
/* Author: 			Barry Shein, bsx@TheWorld.com, 3/1/2013						*/
/*																				*/
/********************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif /* USE_READLINE */

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

static char *prog; 				/* ptr to program name (for message) 	*/
#define DEFAULT_PORT "9000" 	/* must match server default port 		*/

/* Define process exit codes */

#define EX_OK 		0 			/* Normal termination					*/
#define EX_ARGFALL 	1			/* Incorrect arguments					*/
#define EX_SYSERR 	2			/* Error in system call					*/

#define MAXLEN 255

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/* Log - display error or information message for user */

static void Log(char *fmt, ...){
	va_list ap;

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* Fatal - display a fatal error message for user and exit */

static void Fatal(int exval, char *fmt, ...){
	va_list ap;

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);

	exit(exval); 
}

/* getLine - get one line of input from keyboard */

static char *getLine(char *prompt){
#ifdef USE_READLINE
	return(readline(prompt));
#else /* !USE_READLINE */
	char buf[BUFSIZ];	

	(void) fputs(prompt, stdout); /* display the prompt */
	fflush(stdout);

	/* read one line from the keyboard return NULL */
	if(fgets(buf, sizeof(buf), stdin) == NULL){
		return NULL;
	}else{
		char *p; 
	
		/* emulated readline() and strip NEWLINE */
		if((p = strrchr(buf, '\n')) != NULL){
			*p = '\0';
		}

		return (strdup(buf)); /* readline return allocated buffer */
	}
#endif /* !USE_READLINE */
}

/* initClient - initialize and create a connection to the server */

static int initClient(char *host, char *port){
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s; 

	memset(&hints, 0, sizeof(hints));
	
	hints.ai_family 	= AF_UNSPEC; 	/* use IPv4 or IPv6 */
	hints.ai_socktype	= SOCK_STREAM; 	/* stream socket (TCP) */ 

	/* get address of server host */
	if((s = getaddrinfo(host, port, &hints, &result)) != 0){
		Fatal(EX_SYSERR, ANSI_COLOR_RED "%s: getaddrinfo: %s\n" ANSI_COLOR_RESET, prog, gai_strerror(s));
	}

	/* try each address corresponding to name */
	for(rp = result; rp != NULL; rp = rp -> ai_next){
		int sock, ret; /* socket descriptor and return value */
		char hostnum[NI_MAXHOST]; /* host name */

		/* get numeric address of the host for message */
		if((ret = getnameinfo(rp -> ai_addr, rp -> ai_addrlen, hostnum, sizeof(hostnum), NULL, 0, NI_NUMERICHOST)) != 0){
			Log("%s: getnameinfo: %s\n", prog, gai_strerror(ret));
		}else{
			(void) printf("Tryng %s ...\n", hostnum);
			fflush(stdout);
		}

		/* get a new socket */ 
		if((sock = socket(rp -> ai_family, rp -> ai_socktype, rp -> ai_protocol)) < 0){
			if((rp -> ai_family == AF_INET6) && (errno == EAFNOSUPPORT)){
				Log("\nsocket: no IPv6 support on this host\n");
			}else{
				Log("\nsocket: %s\n", strerror(errno));
				continue;
			}
		}

		/* try to connect the new socket to server */
		if(connect(sock, rp -> ai_addr, rp -> ai_addrlen) < 0){
			Log(ANSI_COLOR_RED "connect: %s\n" ANSI_COLOR_RESET, strerror(errno));
			(void) shutdown(sock, SHUT_RDWR);
			continue;
		}else{ /* success */
			(void) printf(ANSI_COLOR_GREEN "connected to %s\n" ANSI_COLOR_RESET, host);
			return(sock);
			break;
		}
	}

	Fatal(EX_ARGFALL, ANSI_COLOR_RED "%s: could not connect to host %s\n" ANSI_COLOR_RESET, prog, host);
	return -1;
}

/* runClient - read from keyboard, send to server, echo response */ 

static void runClient(int sock){ 
	char *sendb, recvb[MAXLEN];
	int bytes, byter;

	memset(recvb, 0, MAXLEN);

	(void) printf(ANSI_COLOR_CYAN "\nWelcome to %s: period newline exits\n\n" ANSI_COLOR_RESET, prog);

	/* read keyboard... */
	while(((sendb = getLine("> ")) != NULL) && (strcmp(sendb, ".") != 0)){
		if((byter = write(sock, sendb, MAXLEN)) <= 0){
			Log(ANSI_COLOR_RED "%s: write: write buffer error\n" ANSI_COLOR_RESET, prog);
			break;
		}

		free(sendb);

		if((bytes = read(sock, recvb, 255)) <= 0){
			Log(ANSI_COLOR_RED "%s: read: read buffer error\n" ANSI_COLOR_RESET, prog);
			break;
		}

		(void) printf(ANSI_COLOR_GREEN "response:" ANSI_COLOR_RESET " %s\n", recvb); /* echo server response */ 
	}
}

/* doneClient - finish: close client */

static void doneClient(int sock){
	if(sock >= 0){
		if(shutdown(sock, SHUT_RDWR) != 0){
			Log(ANSI_COLOR_RED "%s: shutdown error: %s\n" ANSI_COLOR_RESET, strerror(errno));
		}

		Log(ANSI_COLOR_YELLOW "client connection closed\n" ANSI_COLOR_RESET);
	}
} 

/* Usage - helpful command line message */

static void Usage(void){
	(void) printf("Usage: %s [-p port] host\n", prog);
	exit(EX_OK);
}

/* main - parse command line and start client */

int main(int argc, char **argv){
	int c; 
	char *host = NULL;
	char *port = DEFAULT_PORT;
	int sock; 

	prog = strrchr(*argv, '/') ? strrchr(*argv, '/') + 1 : *argv;

	while((c = getopt(argc, argv, "hp:")) != EOF){
		switch(c){
			case 'p': port = optarg; break; 
			case 'h': default: Usage();
		}
	}

	if(optind < argc){
		host = argv[optind++];

		if(optind != argc){
			Log("%s: too many command line args\n", prog);
			Usage();
		}
	}else{
		Log("%s: missing host arg\n", prog);
		Usage();
	}

	sock = initClient(host, port); /* call will exit on error or failure */ 
	runClient(sock);
	doneClient(sock);
	exit(EX_OK);
}
