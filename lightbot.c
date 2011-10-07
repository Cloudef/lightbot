#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BOT_NICK    "TestBot|EX"
#define BOT_PORT    6667
#define BOT_SERVER  "irc.freenode.net"
#define BOT_CHANNEL "#kakkansyojat"

#define BUFFER_SIZE 4096
#define HEADER_PING           "PING :"
#define HEADER_PING_SIZE      6
#define HEADER_PONG           "PONG :"
#define HEADER_PRIVMSG        "PRIVMSG"
#define HEADER_PRIVMSG_SIZE   7

#define NICK_MAX 50
#define MESSAGE_MAX 2048

#define DEBUG 1

#define LENGTH(X)             (sizeof X / sizeof X[0])

static char MODE_NAME[ BUFFER_SIZE ];
int IRC_SOCKET = 0;

typedef enum
{ RETURN_OK = 0, RETURN_FAIL, RETURN_NOTHING
} eRETURN;

typedef void cmd_func( char*, char* );
typedef struct
{
   const char *command;
   cmd_func   *function;
} command_t;

/* helper functions */
void say( char *message, char *target );
void say_highlight( char *message, char *nick, char *target );

/* cmds */
void cmd_test( char *nick, char *message );

/* define cmds */
command_t IRC_CMD[] =
{
   { "!test", cmd_test },
};

void cmd_test( char *nick, char *message )
{
   say( "Jumala Rakastaa", BOT_CHANNEL );
   say( "Jumala Rakastaa", nick );
   say_highlight( "Jumala Rakastaa", nick, BOT_CHANNEL );
}

/* HELPER FUNCTIONS */
void say( char *message, char *target )
{
   char buffer[ BUFFER_SIZE ];

   snprintf(buffer, BUFFER_SIZE,"PRIVMSG %s :%s\r\n", target, message);
   send(IRC_SOCKET, buffer, strlen(buffer), 0);
}

void say_highlight( char *message, char *nick, char *target )
{
   char buffer[ MESSAGE_MAX ];

   snprintf( buffer, MESSAGE_MAX, "%s: %s", nick, message );
   say( buffer, target );
}

/* BOT CODE BELOW */

static int strsplit(char ***dst, char *str, char *token) {
   char *saveptr, *ptr, *start;
   int32_t t_len, i;

   if (!(saveptr=strdup(str)))
      return 0;

   *dst=NULL;
   t_len=strlen(token);
   i=0;

   for (start=saveptr,ptr=start;;ptr++) {
      if (!strncmp(ptr,token,t_len) || !*ptr) {
         while (!strncmp(ptr,token,t_len)) {
            *ptr=0;
            ptr+=t_len;
         }

         if (!((*dst)=realloc(*dst,(i+2)*sizeof(char*))))
            return 0;
         (*dst)[i]=start;
         (*dst)[i+1]=NULL;
         i++;

         if (!*ptr)
            break;
         start=ptr;
      }
   }
   return i;
}

static void strsplit_clear(char ***dst) {
   if ((*dst)[0])
      free((*dst)[0]);
   free((*dst));
}

static int ircconnect( char *irc_server, int port, char *nick )
{
   char buffer[BUFFER_SIZE];
   struct hostent *he;
   struct sockaddr_in server;

   if(!(he = gethostbyname(irc_server)))
   {
      puts("-!- Invalid server");
      return( RETURN_FAIL );
   }
   printf("-!- Hostname: %s\n", he->h_name);
   printf("-!- IP: %s\n", (inet_ntoa(*((struct in_addr *) he->h_addr))));
   printf("-!- Connecting to port: %d\n", port);
   if((IRC_SOCKET = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      puts("-!- Socket creation failed");
      return( RETURN_FAIL );
   }

   server.sin_family = AF_INET;
   server.sin_port   = htons(port);
   server.sin_addr   = *((struct in_addr *)he->h_addr);
   bzero(&(server.sin_zero), 8);

   if(connect(IRC_SOCKET, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
   {
      puts("-!- Connection failed");
      return( RETURN_FAIL );
   }
   puts("-!- Connected");
   puts("-!- Sending nick");

   snprintf(buffer, BUFFER_SIZE,
            "NICK %s\r\nUSER %s \"\" \"%s\" :x\r\n", nick, nick,
            (inet_ntoa(*((struct in_addr *) he->h_addr)))
            );
   send(IRC_SOCKET, buffer, strlen(buffer), 0);
}

static uint8_t CHANNEL_JOINED = 0;
static void joinchannel( char *channel )
{
   char buffer[BUFFER_SIZE];
   if( CHANNEL_JOINED )
      return;

   snprintf(buffer, BUFFER_SIZE, "JOIN %s\r\n", channel);
   send(IRC_SOCKET, buffer, strlen(buffer), 0);

   CHANNEL_JOINED = 1;
   printf("-!- Joined %s\n", channel);
}

static void ping( char *buffer )
{
   size_t i;
   char ID[BUFFER_SIZE];
   char message[ BUFFER_SIZE ];

   memset(ID, '\0', BUFFER_SIZE * sizeof(char));

   i = 6;
   for(; buffer[i] != ' ' && buffer[i] != '\0'; ++i)
      ID[i-6] = buffer[i];
   snprintf( message, BUFFER_SIZE, HEADER_PONG"%s", ID );
#if DEBUG
   puts(message); /* Answer */
#endif

   send(IRC_SOCKET, message, strlen(message), 0 );
   joinchannel( BOT_CHANNEL );
}

static void privmsg( char *nick, char *message )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(IRC_CMD); ++i)
   {
      if(!strncmp(message, IRC_CMD[i].command, strlen(IRC_CMD[i].command)))
         IRC_CMD[i].function( nick, message );
   }
}

static void parsemessage( char *buffer )
{
   size_t i, p;
   uint8_t parse_message = 0;
   char nick[ NICK_MAX + 1 ];
   char message[ MESSAGE_MAX + 1 ];

   memset( nick, '\0', NICK_MAX * sizeof(char));
   memset( message, '\0', MESSAGE_MAX * sizeof(char));

   i = 1;
   for(; buffer[i] != '!'; ++i)
   {
      if( i > NICK_MAX ) return; /* non valid */
      nick[i-1] = buffer[i];
   }

   p = 0;
   for(; i != MESSAGE_MAX; ++i)
   {
      if(parse_message) message[p++] = buffer[i];
      if(buffer[i] == ':') parse_message = 1;
   }
   if(!parse_message) return; /* non valid */

   privmsg( nick, message );
}

static void parsebuffer( char *buffer )
{
   size_t i;

   i = 0;
   for(; buffer[i] != '\0'; ++i)
   {
      /* PRIVMSG */
      if(!strncmp(&buffer[i], HEADER_PRIVMSG, HEADER_PRIVMSG_SIZE))
         parsemessage( buffer );
      /* PING */
      if(!strncmp(&buffer[i], HEADER_PING, HEADER_PING_SIZE))
         ping( &buffer[i] );
      /* MODE SET FOR <NICK> */
      if(!strncmp(&buffer[i], MODE_NAME, strlen(MODE_NAME)))
         joinchannel( BOT_CHANNEL );
   }
}

static void cleanup( int ret )
{
   if(IRC_SOCKET) close(IRC_SOCKET);
   IRC_SOCKET = 0;
   exit(ret);
}

int main(int argc, char *argv[])
{
   size_t bytes, count, i;
   char buffer[BUFFER_SIZE];
   char **split = NULL;

   (void)signal(SIGINT,  cleanup);
   (void)signal(SIGTERM, cleanup);
   (void)signal(SIGSEGV, cleanup);

   if(ircconnect( BOT_SERVER, BOT_PORT, BOT_NICK ) == RETURN_FAIL)
      cleanup( EXIT_FAILURE );

   snprintf( MODE_NAME, BUFFER_SIZE, ":%s MODE %s :", BOT_NICK, BOT_NICK );
   while(1)
   {
      memset(buffer, '\0', BUFFER_SIZE);
      recv(IRC_SOCKET, buffer, BUFFER_SIZE * sizeof(char), 0);
      count = strsplit(&split,buffer,"\r\n");

      i = 0;
      for(; i != count; ++i)
      {
#if DEBUG
         printf("# %s\n", split[i]);
#endif
         parsebuffer(split[i]);
      }
      strsplit_clear(&split);

      sleep(1);
   }

   puts("-! Closing");
   cleanup( EXIT_SUCCESS );
   return( EXIT_SUCCESS ); /* should be never called */
}
