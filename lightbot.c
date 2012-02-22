#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <ctype.h>
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

#define BUFFER_SIZE  4096
#define HEADER_PING           "PING :"
#define HEADER_PONG           "PONG :"
#define HEADER_PRIVMSG        "PRIVMSG"
#define HEADER_JOIN           "JOIN"
#define HEADER_PART           "PART"
#define HEADER_KICK           "KICK"
#define HEADER_NAMES          "NAMES"

#define NICK_MAX     50
#define IDENT_MAX    50
#define CHANNEL_MAX  50
#define MESSAGE_MAX  2048

#define SH_READ_MAX 256

#define DEBUG 1

#define LENGTH(X)             (sizeof X / sizeof X[0])

static char MODE_NAME[ BUFFER_SIZE ];
int IRC_SOCKET = 0;

typedef enum
{ RETURN_OK = 0, RETURN_FAIL, RETURN_NOTHING
} eRETURN;

typedef struct
{
   char nick[ NICK_MAX ];
   char ident[ IDENT_MAX ];
   char channel[ CHANNEL_MAX ];
} user_info;

typedef void cmd_func( const user_info*, const char* );
typedef struct
{
   const char *command;
   cmd_func   *function;
   const char *help;
} command_t;

typedef void join_func( const user_info* );
typedef struct
{
   const char *nick;
   const char *ident;
   const char *priv;
   join_func  *joinfunc;
   join_func  *partfunc;
} user_t;

typedef struct ban_t
{
   user_info    *user;
   char         *reason;
   struct ban_t *next;
} ban_t;
static ban_t *IRC_BAN = NULL;

typedef struct usr_t
{
   user_info    *user;
   struct usr_t *next;
} usr_t;
static usr_t *IRC_USR = NULL;

/* split funcs */
static int strsplit(char ***dst, const char *str, const char *token);
static void strsplit_clear(char ***dst);

/* replace func (check for NULL, and remember free if !NULL) */
static char *str_replace(const char *s, const char *old, const char *new);

/* helper functions */
static user_info* getusr( const char *nick, const char *channel );
static int hasban( const user_info *user );
static int isop( const user_info *user );
static void say( const char *message, const char *target );
static void say_highlight( const char *message, const user_info *user );
static void set_mode( const user_info *user, const char *level );
static void set_mode_channel( const char *channel, const char *level );
static void set_topic( const char *channel, const char *topic );
static void kick( const user_info *user, const char *reason );
static void ban( const user_info *user, const char *reason );
static void unban( const user_info *user );
static size_t sh_run( const char *cmd, char output[][SH_READ_MAX], size_t lines );

/* cmds */
static void cmd_test( const user_info *user, const char *message );
static void cmd_kick( const user_info *user, const char *message );
static void cmd_ban( const user_info *user, const char *message );
static void cmd_unban( const user_info *user, const char *message );
static void cmd_op( const user_info *user, const char *message );
static void cmd_deop( const user_info *user, const char *message );
static void cmd_help( const user_info *user, const char *message );
static void cmd_topic( const user_info *user, const char *message );

/* define cmds */
static const command_t MSG_CMD[] =
{
   /* COMMAND, FUNCTION, DESCRIPTION */
   { "!help", cmd_help,    "Help" },
   { "!topic", cmd_topic,  "Topic" },
   { "!test", cmd_test,    "Test" },
   { "!kick", cmd_kick,    "Kick" },
   { "!unban", cmd_unban,  "Unban" },
   { "!ban",  cmd_ban,     "Ban" },
   { "!deop", cmd_deop,    "Deop" },
   { "!op", cmd_op,        "Op" },
};

/* define users */
static const user_t IRC_PRIV[] =
{
   /* NICK, IDENT, PRIVILIDGES, JOIN FUNCTION, PART FUNCTION */
   { "Admin", NULL, "+o", NULL, NULL },
};

static void cmd_help( const user_info *user, const char *message )
{
   size_t i;
   char buffer[256];

   i = 0;
   say( "Commands:", user->nick );
   for(; i != LENGTH( MSG_CMD ); ++i)
   {
      snprintf(buffer, 256, "%s - %s", MSG_CMD[i].command, MSG_CMD[i].help);
      say( buffer, user->nick );
   }
}


static void cmd_unban( const user_info *user, const char *message )
{
   char **split = NULL;
   int count = 0, i;

   if(!isop(user))
      return;

   if(!strlen(message)) return;

   count = strsplit(&split,message," ");
   i = 0;
   for(; i != count; ++i)
      unban( getusr(split[i], user->channel) );
   strsplit_clear(&split);
   if(!count) { unban( getusr(message, user->channel) ); return; }
}

static void cmd_ban( const user_info *user, const char *message )
{
   char nick[ NICK_MAX ];
   char reason[ strlen(message) ];
   int i, p;

   if(!isop(user))
      return;

   if(!strlen(message)) return;

   memset( nick,     0, NICK_MAX * sizeof(char));
   memset( reason,   0, strlen(message) * sizeof(char));

   /* nick */
   p = 0; i = 0;
   for(; i != strlen(message) && !isspace(message[i]); ++i)
      nick[p++] = message[i];

   /* reason */
   p = 0; i++;
   for(; i < strlen(message); ++i)
      reason[p++] = message[i];

   ban( getusr(nick, user->channel), reason );
}

static void cmd_kick( const user_info *user, const char *message )
{
   char nick[ NICK_MAX ];
   char reason[ strlen(message) ];
   int i, p;

   if(!isop(user))
      return;

   if(!strlen(message)) return;

   memset( nick,     0, NICK_MAX * sizeof(char));
   memset( reason,   0, strlen(message) * sizeof(char));

   /* nick */
   p = 0; i = 0;
   for(; i != strlen(message) && !isspace(message[i]); ++i)
      nick[p++] = message[i];

   /* reason */
   p = 0; i++;
   for(; i < strlen(message); ++i)
      reason[p++] = message[i];

   kick( getusr(nick, user->channel), reason );
}

static void cmd_op( const user_info *user, const char *message )
{
   char **split = NULL;
   int count = 0, i;

   if(!isop(user))
      return;

   if(!strlen(message)) { set_mode( user, "+o" ); return; }

   count = strsplit(&split,message," ");
   i = 0;
   for(; i != count; ++i)
      set_mode( getusr(split[i], user->channel), "+o" );
   strsplit_clear(&split);
   if(!count) { set_mode( getusr(message, user->channel), "+o" ); return; }
}

static void cmd_deop( const user_info *user, const char *message )
{
   char **split = NULL;
   int count = 0, i;

   if(!isop(user))
      return;

   if(!strlen(message)) { set_mode( user, "-o" ); return; }

   count = strsplit(&split,message," ");
   i = 0;
   for(; i != count; ++i)
      set_mode( getusr(split[i], user->channel), "-o" );
   strsplit_clear(&split);
   if(!count) { set_mode( getusr(message, user->channel), "-o" ); return; }
}

static void cmd_topic( const user_info *user, const char *message )
{
   if(!isop(user))
      return;

   set_topic( user->channel, message );
}

static void cmd_test( const user_info *user, const char *message )
{
   say_highlight( "Hello World!", user );
}

/* JOIN SIGNAL */
static void JOIN( const user_info *user )
{
   if(!hasban(user)) say_highlight( "Welcome!", user );
}

/* PART SIGNAL */
static void PART( const user_info *user )
{
   say_highlight( "Goodbye!", user );
}

/* HELPER FUNCTIONS */
static int isop( const user_info *user )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(IRC_PRIV); ++i)
   {
      if(IRC_PRIV[i].ident)
      {
         if(!strcmp(user->nick, IRC_PRIV[i].nick) &&
            !strstr(user->ident, IRC_PRIV[i].ident))
         {
            if(strstr(IRC_PRIV[i].priv, "o")) return 1;
         }
      } else
      {
         if(!strcmp(user->nick, IRC_PRIV[i].nick))
         {
            if(strstr(IRC_PRIV[i].priv, "o")) return 1;
         }
      }
   }

   return 0;
}

static uint8_t SAY_COUNT = 0;
static void say( const char *message, const char *target )
{
   char buffer[ BUFFER_SIZE ];

   snprintf(buffer, BUFFER_SIZE,"PRIVMSG %s :%s\r\n", target, message);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);

   if(++SAY_COUNT==5)
   { SAY_COUNT = 0; usleep( 500 * 1000 ); /* lol flood avoid, should be replaced with queue */ }
}

static void say_highlight( const char *message, const user_info *user )
{
   char buffer[ MESSAGE_MAX ];
   if(!user) return;

   snprintf( buffer, MESSAGE_MAX, "%s: %s", user->nick, message );
   say( buffer, user->channel );
}

static void kick( const user_info *user, const char *reason )
{
   char buffer[ BUFFER_SIZE ];
   if(!user) return;

   if(reason && strlen(reason))
        snprintf(buffer, BUFFER_SIZE,"KICK %s %s :%s\r\n", user->channel, user->nick, reason);
   else snprintf(buffer, BUFFER_SIZE,"KICK %s %s\r\n", user->channel, user->nick);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);
}

static void set_channel_mode( const char *channel, const char *level )
{
   char buffer[ BUFFER_SIZE ];

   snprintf(buffer, BUFFER_SIZE,"MODE %s %s\r\n", channel, level);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);
}

static void set_mode( const user_info *user, const char *level )
{
   char buffer[ BUFFER_SIZE ];
   if(!user) return;

   snprintf(buffer, BUFFER_SIZE,"MODE %s %s %s\r\n", user->channel, level, user->nick);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);
}

static void set_topic( const char *channel, const char *topic )
{
   char buffer[ BUFFER_SIZE ];

   if(!topic) return;
   if(!strlen(topic)) return;

   snprintf(buffer, BUFFER_SIZE,"TOPIC %s :%s\r\n", channel, topic);
#if DEBUG
   printf("$ %s\n", buffer);
#endif
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);
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

static void clearbans(void)
{
   ban_t *c = IRC_BAN, *next = NULL;

   for(; c; c = next)
   { next = c->next; free(c); }
   IRC_BAN = NULL;
}

static void delban(ban_t *b )
{
   ban_t *c = IRC_BAN, **cc = &IRC_BAN;

   if(!b) return;

   for(; c && c->next && c->next != b; c = c->next)
      cc = &c;
   (*cc)->next = b->next;

#if DEBUG
   printf( "$ DELBAN %s!%s %s\n", b->user->nick, b->user->ident, b->user->channel );
#endif

   if(b == IRC_BAN) IRC_BAN = NULL;
   free(b->user);
   free(b);
}

static int hasban( const user_info *user )
{
   ban_t *c = IRC_BAN;

   for(; c; c = c->next)
      if( !strcmp(c->user->nick,  user->nick)  ||
          !strcmp(c->user->ident, user->ident) )
         return 1;
   return 0;
}

static void unban( const user_info *user )
{
   char message[ BUFFER_SIZE ];
   ban_t *c = IRC_BAN;

   if(!user) return;

   for(; c; c = c->next)
      if( !strcmp(c->user->nick,  user->nick)  ||
          !strcmp(c->user->ident, user->ident) )
      {
         delban(c);
         snprintf( message, BUFFER_SIZE, "* Unbanned %s", user->nick);
         say( message, user->channel ); return;
      }
}

static void ban( const user_info *user, const char *reason )
{
   ban_t *c;

   if(!user) return;
   if(hasban(user)) return;

   if(!IRC_BAN) { IRC_BAN = malloc( sizeof(ban_t) ); c = IRC_BAN; }
   else {
      c = IRC_BAN; for(; c && c->next; c = c->next);
      c->next = malloc( sizeof(ban_t) ); c = c->next;
   }
   if(!c) return;

   c->user = malloc( sizeof(user_info) );
   if(!c->user)   { delban(c); return; }
   memcpy( c->user, user, sizeof(user_info) );
   c->reason = strdup(reason);
   if(!c->reason) { delban(c); return; }
   c->next = NULL;

#if DEBUG
   printf( "$ BANUSR %s!%s %s\n", user->nick, user->ident, user->channel );
#endif

   /* finally kick */
   kick( c->user, c->reason );
}

static void clearusrs(void)
{
   usr_t *c = IRC_USR, *next = NULL;

   for(; c; c = next)
   { next = c->next; free(c); }
   IRC_USR = NULL;
}

static user_info* getusr( const char *nick, const char *channel )
{
   usr_t *c = IRC_USR;
   ban_t *b = IRC_BAN;

   for(; c; c = c->next)
      if( !strcmp(c->user->nick,    nick)  &&
          !strcmp(c->user->channel, channel))
         return c->user;

   /* might be banned too */
   for(; b; b = b->next)
      if( !strcmp(b->user->nick,    nick)  &&
          !strcmp(b->user->channel, channel))
         return b->user;

   return NULL;
}

static int hasusr( const user_info *user )
{
   usr_t *c = IRC_USR;

   for(; c; c = c->next)
      if( !strcmp(c->user->nick,  user->nick)  &&
          !strcmp(c->user->ident, user->ident) &&
          !strcmp(c->user->channel, user->channel))
         return 1;
   return 0;
}

static void delusr(usr_t *b)
{
   usr_t *c = IRC_USR, **cc = &IRC_USR;

   if(!b) return;

   for(; c && c->next && c->next != b; c = c->next)
      cc = &c;
   (*cc)->next = b->next;

#if DEBUG
   printf( "$ DELUSR %s!%s %s\n", b->user->nick, b->user->ident, b->user->channel );
#endif

   if(b == IRC_USR) IRC_USR = NULL;
   free(b->user);
   free(b);
}

static void unusr_channel( const char *channel )
{
   usr_t *c = IRC_USR;

   for(; c; c = c->next)
      if( !strcmp(c->user->channel, channel))
      { delusr(c); }
}

static void unusr( const user_info *user )
{
   usr_t *c = IRC_USR;

   for(; c; c = c->next)
      if( !strcmp(c->user->nick,  user->nick)  &&
          !strcmp(c->user->ident, user->ident) &&
          !strcmp(c->user->channel, user->channel))
      { delusr(c); return; }
}

static void addusr( const user_info *user )
{
   usr_t *c;

   if(hasban(user)) kick( user, "You are banned" );
   if(hasusr(user)) return;

   if(!IRC_USR) { IRC_USR = malloc( sizeof(usr_t) ); c = IRC_USR; }
   else {
      c = IRC_USR; for(; c && c->next; c = c->next);
      c->next = malloc( sizeof(usr_t) ); c = c->next;
   }
   if(!c) return;

   c->user = malloc( sizeof(user_info) );
   if(!c->user)   { delusr(c); return; }
   memcpy( c->user, user, sizeof(user_info) );
   c->next = NULL;

#if DEBUG
   printf( "$ ADDUSR %s!%s %s\n", user->nick, user->ident, user->channel );
#endif

   /* check if user is OP */
   if(!isop(user))
      return;

   /* OP him */
   set_mode(user, "+o");
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

static int strsplit(char ***dst, const char *str, const char *token) {
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
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);

   return( RETURN_OK );
}

static size_t parseuserinfo( user_info *user, char *buffer, const char *CMD )
{
   size_t i, p;
   uint8_t parse_channel = 0;

   memset( user->nick,     0, NICK_MAX    * sizeof(char));
   memset( user->ident,    0, IDENT_MAX   * sizeof(char));
   memset( user->channel,  0, CHANNEL_MAX * sizeof(char));

   /* read nick */
   p = 0; i = 1;
   for(; buffer[i] != '!'; ++i)
   {
      if( p > NICK_MAX ) return 0; /* non valid */
      user->nick[p++] = buffer[i];
   }

   /* read ident */
   p = 0; i++;
   for(; !isspace(buffer[i]); ++i)
   {
      if( p > IDENT_MAX ) return 0; /* non valid */
      if(buffer[i] == '~') continue;
      user->ident[p++] = buffer[i];
   }

   /* read target */
   p = 0;
   for(; i != strlen(buffer); ++i)
   {
      if(p == CHANNEL_MAX) break; /* max reached */
      if(!strncmp(&buffer[i], CMD, strlen(CMD))) { parse_channel = 1; i += strlen(CMD) + 1; }
      if(buffer[i] == ':') continue;
      if(parse_channel)
      {  if(isspace(buffer[i])) break;
         user->channel[p++] = buffer[i]; }
   }
   if(!parse_channel) return 0; /* non valid */
   if(!strcmp(user->channel, BOT_NICK))
      snprintf( user->channel, CHANNEL_MAX, "%s", user->nick ); /* PRIVATE MESSAGE */

   return(i); /* return amount read */
}

static uint8_t CHANNEL_JOINED = 0;
static void joinchannel( const char *channel )
{
   char buffer[BUFFER_SIZE];
   if( CHANNEL_JOINED )
      return;

   snprintf(buffer, BUFFER_SIZE, "JOIN %s\r\n", channel);
   send(IRC_SOCKET, buffer, strlen(buffer) * sizeof(char), 0);
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

   send(IRC_SOCKET, message, strlen(message) * sizeof(char), 0 );
   joinchannel( BOT_CHANNEL );
}

static void privmsg( const user_info *user, const char *message )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(MSG_CMD); ++i)
   {
      if(!strncmp(message, MSG_CMD[i].command, strlen(MSG_CMD[i].command)))
      {  if(strlen(MSG_CMD[i].command)+1 > strlen(message)) MSG_CMD[i].function( user, "" );
         else MSG_CMD[i].function( user, message+strlen(MSG_CMD[i].command)+1 );
         return; }
   }
}

static void parsemessage( char *buffer )
{
   size_t i, p;
   uint8_t parse_message = 0;
   char message[ MESSAGE_MAX + 1 ];

   memset( message, '\0', MESSAGE_MAX * sizeof(char));

   user_info user;
   i = parseuserinfo( &user, buffer, HEADER_PRIVMSG );
   if(!i) return;

   /* read message */
   p = 0;
   for(; i != strlen(buffer); ++i)
   {
      if(p == MESSAGE_MAX) break; /* max reached */
      if(parse_message) message[p++] = buffer[i];
      if(buffer[i] == ':') parse_message = 1;
   }
   if(!parse_message) return; /* non valid */

   addusr( &user ); /* if he's been AFK on channel, and bot joined later */
   privmsg( &user, message );
}

static void joinhandle( const user_info *user )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(IRC_PRIV); ++i)
   {
      if(!strcmp(user->nick, IRC_PRIV[i].nick))
      {
         if(IRC_PRIV[i].joinfunc) IRC_PRIV[i].joinfunc( user );
         if(IRC_PRIV[i].priv) set_mode( user, IRC_PRIV[i].priv );
         return;
      }
   }
}

static void parthandle( const user_info *user )
{
   size_t i;

   i = 0;
   for(; i != LENGTH(IRC_PRIV); ++i)
   {
      if(!strcmp(user->nick, IRC_PRIV[i].nick))
      {
         if(IRC_PRIV[i].partfunc) IRC_PRIV[i].partfunc( user );
         return;
      }
   }
}

static void parsejoinpart( char *buffer, uint8_t part )
{
   user_info user;

   if(!part) {
      if(!parseuserinfo( &user, buffer, HEADER_JOIN )) return;
      if(!strcmp(user.nick, BOT_NICK))
      {
         CHANNEL_JOINED = 1;
         printf("-!- Joined %s\n", user.channel);
         return;
      }
      addusr( &user );
      joinhandle( &user );
      JOIN( &user );
   }
   else {
      if(!parseuserinfo( &user, buffer, HEADER_PART )) return;
      if(!strcmp(user.nick, BOT_NICK))
      {
         CHANNEL_JOINED = 0;
         printf("-!- Parted %s\n", user.channel);
         unusr_channel( user.channel );
         return;
      }
      unusr( &user );
      parthandle( &user );
      PART( &user );
   }
}

static void parsebuffer( char *buffer )
{
   size_t i;

   i = 0;
   for(; buffer[i] != '\0'; ++i)
   {
      /* PRIVMSG */
      if(!strncmp(buffer + i, HEADER_PRIVMSG, strlen(HEADER_PRIVMSG)))
      { parsemessage( buffer ); break; }
      /* JOIN */
      else if(!strncmp(buffer + i, HEADER_JOIN, strlen(HEADER_JOIN)))
      { parsejoinpart( buffer, 0 ); break; }
      /* PART */
      else if(!strncmp(buffer + i, HEADER_PART, strlen(HEADER_PART)))
      { parsejoinpart( buffer, 1 ); break; }
       /* PART */
      else if(!strncmp(buffer + i, HEADER_KICK, strlen(HEADER_KICK)))
      { parsejoinpart( buffer, 1 ); break; }
      /* PING */
      else if(!strncmp(buffer + i, HEADER_PING, strlen(HEADER_PING)))
      { ping( buffer + i ); break; }
      /* MODE SET FOR <NICK> */
      else if(!strncmp(buffer + i, MODE_NAME, strlen(MODE_NAME)))
      { joinchannel( BOT_CHANNEL ); break; }
   }
}

static void cleanup( int ret )
{
   clearbans();
   clearusrs();
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
      send(IRC_SOCKET, "\r\n", strlen("\r\n") * sizeof(char), 0); /* flush */
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
   }

   puts("-! Closing");
   cleanup( EXIT_SUCCESS );
   return( EXIT_SUCCESS ); /* should be never called */
}
