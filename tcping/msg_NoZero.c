#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8085    /* the port client will be connecting to */
#define IP_HEX(a,b,c,d) ((a)<<24|(b)<<16|(c)<<8|(d))
#define SERVER_ADDR     IP_HEX(10,128,0,5)

#define MAXDATASIZE (1<<14) /* max number of bytes we can get at once */

char buf[MAXDATASIZE];

int main(int argc, char *argv[])
{
    int sockfd, numbytes;
    int one = 1;
    int cnt = (1<<25);
    struct sockaddr_in their_addr; /* connector's address information */


    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    their_addr.sin_family = AF_INET;      /* host byte order */
    their_addr.sin_port = htons(PORT);    /* short, network byte order */
    their_addr.sin_addr.s_addr = htonl(SERVER_ADDR);
    bzero(&(their_addr.sin_zero), 8);     /* zero the rest of the struct */

//    if (setsockopt(sockfd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one))) {
  //      perror("setsockopt zerocopy");
//	exit(1);
  //   }

    if (connect(sockfd, (struct sockaddr *)&their_addr, \
                                          sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }
    while (cnt--) {
    	if (send(sockfd, buf, sizeof(buf),  0) == -1){
                  perror("send");
    	      exit (1);
    	}
//printf("After the send function \n");
//
//if ((numbytes=recv(sockfd, buf, MAXDATASIZE, 0)) == -1) {
//		perror("recv");
//		exit(1);
//}
//
//    buf[numbytes] = '\0';
//
//printf("Received in pid=%d, text=: %s \n",getpid(), buf);
//sleep(1);

    }

    close(sockfd);

    return 0;
}
