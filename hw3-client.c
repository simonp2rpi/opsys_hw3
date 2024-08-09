/* hw3-client.c */

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char ** argv )
{
  /* create TCP client socket (endpoint) */
  int sd = socket( AF_INET, SOCK_STREAM, 0 );
  if ( sd == -1 ) { perror( "socket() failed" ); exit( EXIT_FAILURE ); }

  struct hostent * hp = gethostbyname( "localhost" );

#if 0
  struct hostent * hp = gethostbyname( "127.0.0.1" );
  struct hostent * hp = gethostbyname( "128.113.126.39" );
  struct hostent * hp = gethostbyname( "linux02.cs.rpi.edu" );
#endif

  /* TO DO: rewrite the code above to use getaddrinfo() */

  if ( hp == NULL )
  {
    fprintf( stderr, "ERROR: gethostbyname() failed\n" );
    return EXIT_FAILURE;
  }

  struct sockaddr_in tcp_server;
  tcp_server.sin_family = AF_INET;  /* IPv4 */
  memcpy( (void *)&tcp_server.sin_addr, (void *)hp->h_addr, hp->h_length );
  unsigned short server_port = 8000;
  if (argc == 2) {
    server_port = atoi(*(argv + 1));
  }
  tcp_server.sin_port = htons( server_port );

  printf( "CLIENT: TCP server address is %s\n", inet_ntoa( tcp_server.sin_addr ) );

  printf( "CLIENT: connecting to server...\n" );

  if ( connect( sd, (struct sockaddr *)&tcp_server, sizeof( tcp_server ) ) == -1 )
  {
    perror( "connect() failed" );
    return EXIT_FAILURE;
  }


  /* The implementation of the application protocol is below... */
int extra = 0, length = 0;
while ( 1 )    /* TO DO: fix the memory leaks! */
{
  int flag = 0;
  char * buffer = calloc( 255, sizeof( char ) );
  if ( fgets( buffer, 255, stdin ) == NULL ) break;
  // if ( strlen( buffer ) != 6 ) { printf( "CLIENT: invalid -- try again\n" ); continue; }
  length = strlen(buffer) - 1;
  *(buffer + length) ='\0';   /* get rid of the '\n' */

  printf( "CLIENT: Sending to server: %s\n", buffer );
  int n = write( sd, buffer, strlen( buffer ) );    /* or use send()/recv() */
  if ( n == -1 ) { perror( "write() failed" ); return EXIT_FAILURE; }

  for (int i = 0; i < (length + extra) / 5; i++) {
    printf("%s", buffer);
    fprintf(stderr, "write() returned 0: %s\n", strerror(errno));
    n = read( sd, buffer, 9 );    /* BLOCKING */
    printf("%d\n", n);
    if ( n == -1 )
    {
      perror( "read() failed" );
      return EXIT_FAILURE;
    }
    else if ( n == 0 )
    {
      printf( "CLIENT: rcvd no data; TCP server socket was closed\n" );
      flag = 1;
      break;
    }
    else /* n > 0 */
    {
      switch ( *buffer )
      {
        case 'N': printf( "CLIENT: invalid guess -- try again" ); break;
        case 'Y': printf( "CLIENT: response: %s", buffer + 3 ); break;
      }

      short guesses = ntohs( *(short *)(buffer + 1) );
      printf( " -- %d guess%s remaining\n", guesses, guesses == 1 ? "" : "es" );
      if ( guesses == 0 )
      {
          flag = 1;
          break;
          printf( "CLIENT: you lost!\n" );
      }
      int total = 0;
      for (int i = 3; i < 8; i++)
      {
          if (!(*(buffer + i) < 65 || *(buffer + i) > 90))
          {
              total++;
          }
      }
      if (total == 5)
      {
          printf( "CLIENT: you won!\n" );
          flag = 1;
          break;
      }
    }
  }
  extra = (length + extra) % 5;
  if (flag) {break;}
}


  printf( "CLIENT: disconnecting...\n" );

  close( sd );

  return EXIT_SUCCESS;
}