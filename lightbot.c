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

#define BOT_NICK    "CrappyBot"
#define BOT_PORT    6667
#define BOT_SERVER  "irc.freenode.net"
#define BOT_CHANNEL "#test" /* only one channel supported right now */

#define BUFFER_SIZE 4096
#define HEADER_PING           "PING :"
#define HEADER_PING_SIZE      6
#define HEADER_PONG           "PONG :"
#define HEADER_PRIVMSG        "PRIVMSG"
#define HEADER_PRIVMSG_SIZE   7
#define HEADER_JOIN           "JOIN"
#define HEADER_JOIN_SIZE      4
#define HEADER_PART           "PART"
#define HEADER_PART_SIZE      4
#define HEADER_KICK           "KICK"
#define HEADER_KICK_SIZE      4

#define NICK_MAX 50
#define CHANNEL_MAX 50
#define MESSAGE_MAX 2048

#define SH_READ_MAX 256

#define DEBUG 1

#define LENGTH(X)             (sizeof X / sizeof X[0])

static char MODE_NAME[ BUFFER_SIZE ];
int IRC_SOCKET = 0;

typedef enum
{ RETURN_OK = 0, RETURN_FAIL, RETURN_NOTHING
} eRETURN;

typedef void cmd_func( const char*, const char* );
typedef struct
{
   const char *command;
   cmd_func   *function;
   const char *help;
} command_t;

typedef struct
{
   const char *nick;
   const char *priv;
   cmd_func   *joinfunc;
   cmd_func   *partfunc;
} user_t;

/* split funcs */
static int strsplit(char ***dst, char *str, char *token);
static void strsplit_clear(char ***dst);

/* replace func (check for NULL, and remember free if !NULL) */
static char *str_replace(const char *s, const char *old, const char *new);

/* helper functions */
static void say( const char *message, const char *target );
static void say_highlight( const char *message, const char *nick, const char *target );
static void set_mode( const char *channel, const char *nick, const char *level );
static size_t sh_run( const char *cmd, char output[][SH_READ_MAX], size_t lines );

/* cmds */
static void cmd_test( const char *nick, const char *message );
static void cmd_help( const char *nick, const char *message );

/* define cmds */
static const command_t MSG_CMD[] =
{
   /* COMMAND, FUNCTION, DESCRIPTION */
   { "!help", cmd_help,    "Show help" },
   { "!test", cmd_test,    "Test command" },
};

/* define users */
static const user_t IRC_USR[] =
{
   /* NICK, PRIVILIDGES, JOIN FUNCTION, PART FUNCTION */
   { "Admin", "+o",      NULL, NULL },
};

static void cmd_help( const char *nick, const char *message )
{
   size_t i;
   char buffer[256];

   i = 0;
   say( "Commands:", nick );
   for(; i != LENGTH( MSG_CMD ); ++i)
   {
      snprintf(buffer, 256, "%s - %s", MSG_CMD[i].command, MSG_CMD[i].help);
      say( buffer, nick );
   }
}

static void cmd_test( const char *nick, const char *message )
{
   say_highlight( "Hello World!", nick, BOT_CHANNEL );
}

/* JOIN SIGNAL */
static void JOIN( const char *nick, const char *channel )
{
   say_highlight( "Welcome!", nick, BOT_CHANNEL );
}

/* PART SIGNAL */
static void PART( const char *nick, const char *channel )
{
   say_highlight( "Goodbye!", nick, BOT_CHANNEL );
}

/* HELPER FUNCTIONS */
static uint8_t SAY_COUNT = 0;
static void say( const char *message, const char *target )
{
   char buffer[ BUFFER_SIZE ];

   snprintf(buffer, BUFFER_SIZE,"PRIVMSG %s :%s\r\n", target, message);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer), 0);

   if(++SAY_COUNT==5)
   { SAY_COUNT = 0; usleep( 500 * 1000 ); /* lol flood avoid, should be replaced with queue */ }
}

static void say_highlight( const char *message, const char *nick, const char *target )
{
   char buffer[ MESSAGE_MAX ];

   snprintf( buffer, MESSAGE_MAX, "%s: %s", nick, message );
   say( buffer, target );
}

static void set_channel_mode( const char *channel, const char *level )
{
   char buffer[ BUFFER_SIZE ];

   snprintf(buffer, BUFFER_SIZE,"MODE %s %s\r\n", channel, level);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer), 0);
}

static void set_mode( const char *channel, const char *nick,  const char *level )
{
   char buffer[ BUFFER_SIZE ];

   snprintf(buffer, BUFFER_SIZE,"MODE %s %s %s\r\n", channel, level, nick);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer), 0);
}

static size_t sh_run( const char *cmd, char output[][SH_READ_MAX], size_t lines )
{
   size_t nbytes, i;
   FILE *pipe;

#if DEBUG
   printf("$ %s\n", cmd);
#endif

   pipe = popen(cmd, "r");
   if (!pipe) return( 0 );

   i = 0;
   while(fgets(output[i], SH_READ_MAX, pipe) != NULL)
   { if(lines && i == lines) break; i++; }

   pclose(pipe);
   return( i );
}

/* BOT CODE BELOW */
static char *str_replace(const char *s, const char *old, const char *new)
{
  size_t slen = strlen(s)+1;
  char *cout=0, *p=0, *tmp=NULL; cout=malloc(slen); p=cout;
  if( !p )
    return 0;
  while( *s )
    if( !strncmp(s, old, strlen(old)) )
    {
      p  -= (intptr_t)cout;
      cout= realloc(cout, slen += strlen(new)-strlen(old) );
      tmp = strcpy(p=cout+(intptr_t)p, new);
      p  += strlen(tmp);
      s  += strlen(old);
    }
    else
     *p++=*s++;

  *p=0;
  return cout;
}

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

static int ircconnect( const char *irc_server, int port, const char *nick )
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
static void joinchannel( const char *channel )
{
   char buffer[BUFFER_SIZE];
   if( CHANNEL_JOINED )
      return;

   snprintf(buffer, BUFFER_SIZE, "JOIN %s\r\n", channel);
   send(IRC_SOCKET, buffer, strlen(buffer), 0);
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

static void privmsg( const char *nick, const char *message )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(MSG_CMD); ++i)
   {
      if(!strncmp(message, MSG_CMD[i].command, strlen(MSG_CMD[i].command)))
      { MSG_CMD[i].function( nick, message+strlen(MSG_CMD[i].command)+1 ); return; }
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
      if( i-1 > NICK_MAX ) return; /* non valid */
      nick[i-1] = buffer[i];
   }

   p = 0;
   for(; i != BUFFER_SIZE; ++i)
   {
      if(p == MESSAGE_MAX) break; /* max reached */
      if(parse_message) message[p++] = buffer[i];
      if(buffer[i] == ':') parse_message = 1;
   }
   if(!parse_message) return; /* non valid */

   privmsg( nick, message );
}

static void joinhandle( const char *nick, const char *channel )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(MSG_CMD); ++i)
   {
      if(!strcmp(nick, IRC_USR[i].nick))
      {
         if(IRC_USR[i].joinfunc) IRC_USR[i].joinfunc( nick, channel );
         if(IRC_USR[i].priv) set_mode( channel, nick, IRC_USR[i].priv );
         return;
      }
   }
}

static void parthandle( const char *nick, const char *channel )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(MSG_CMD); ++i)
   {
      if(!strcmp(nick, IRC_USR[i].nick))
      {
         if(IRC_USR[i].partfunc) IRC_USR[i].partfunc( nick, channel );
         return;
      }
   }
}

static void parsejoinpart( char *buffer, uint8_t part )
{
   size_t i, p;
   uint8_t parse_channel = 0;
   char nick[ NICK_MAX + 1 ];
   char channel[ CHANNEL_MAX + 1 ];

   memset( nick, '\0', NICK_MAX * sizeof(char));
   memset( channel, '\0', CHANNEL_MAX * sizeof(char));

   i = 1;
   for(; buffer[i] != '!'; ++i)
   {
      if( i-1 > NICK_MAX ) return; /* non valid */
      nick[i-1] = buffer[i];
   }

   p = 0;
   for(; i != BUFFER_SIZE; ++i)
   {
      if(p == CHANNEL_MAX) break; /* max reached */
      if(buffer[i] == '#') parse_channel = 1;
      if(parse_channel)
      {  if(isspace(buffer[i])) break;
         channel[p++] = buffer[i]; }
   }
   if(!parse_channel) return; /* non valid */

   if(!part) {
      if(!strcmp(nick, BOT_NICK))
      {
         CHANNEL_JOINED = 1;
         printf("-!- Joined %s\n", channel);
         return;
      }
      joinhandle( nick, channel );
      JOIN( nick, channel );
   }
   else {
      if(!strcmp(nick, BOT_NICK))
      {
         CHANNEL_JOINED = 0;
         printf("-!- Parted %s\n", channel);
         return;
      }
      PART( nick, channel );
   }
}

static void parsebuffer( char *buffer )
{
   size_t i;

   i = 0;
   for(; buffer[i] != '\0'; ++i)
   {
      if(i != 0 && buffer[i] == ':') return;
      /* PRIVMSG */
      if(!strncmp(&buffer[i], HEADER_PRIVMSG, HEADER_PRIVMSG_SIZE))
         parsemessage( buffer );
      /* JOIN */
      if(!strncmp(&buffer[i], HEADER_JOIN, HEADER_JOIN_SIZE))
         parsejoinpart( buffer, 0 );
      /* PART */
      if(!strncmp(&buffer[i], HEADER_PART, HEADER_PART_SIZE))
         parsejoinpart( buffer, 1 );
       /* PART */
      if(!strncmp(&buffer[i], HEADER_KICK, HEADER_KICK_SIZE))
         parsejoinpart( buffer, 1 );
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
   (void)signal(SIGCHLD, SIG_IGN);

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
      if(!strlen(buffer)) break; /* something is wrong */

      sleep(1);
   }

   puts("-! Closing");
   cleanup( EXIT_SUCCESS );
   return( EXIT_SUCCESS ); /* should be never called */
}
