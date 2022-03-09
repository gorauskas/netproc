
/*
 *  Copyright (C) 2020-2021 Mayco S. Berghetti
 *
 *  This file is part of Netproc.
 *
 *  Netproc is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// references
// https://www.kernel.org/doc/Documentation/networking/proc_net_tcp.txt

#include <errno.h>  // variable errno
#include <stdbool.h>
#include <stdio.h>        // FILE *
#include <string.h>       // strlen, strerror
#include <netinet/tcp.h>  // TCP_ESTABLISHED, TCP_TIME_WAIT...

#include "connection.h"
#include "hashtable.h"
#include "jhash.h"
#include "config.h"  // define TCP | UDP
#include "m_error.h"
#include "macro_util.h"

static hashtable_t *ht_connections = NULL;

// values to key_type
#define KEY_INODE 1
#define KEY_TUPLE 2

static int key_type;

void
print_tuple ( struct tuple *tp1, struct tuple *tp2 )
{
  fprintf ( stderr,
            "tuple 1\n"
            "local ip - %d\n"
            "remote ip - %d\n"
            "protocol - %d\n"
            "local_port - %d\n"
            "remote_port - %d\n\n",
            tp1->l3.local.ip,
            tp1->l3.remote.ip,
            tp1->l4.protocol,
            tp1->l4.local_port,
            tp1->l4.remote_port );

  fprintf ( stderr,
            "tuple 2\n"
            "local ip - %d\n"
            "remote ip - %d\n"
            "protocol - %d\n"
            "local_port - %d\n"
            "remote_port - %d\n\n",
            tp2->l3.local.ip,
            tp2->l3.remote.ip,
            tp2->l4.protocol,
            tp2->l4.local_port,
            tp2->l4.remote_port );
}

static hash_t
ht_cb_hash ( const void *key )
{
  size_t size;
  switch ( key_type )
    {
      case KEY_INODE:
        size = SIZEOF_MEMBER ( connection_t, inode );
        break;
      case KEY_TUPLE:
        size = SIZEOF_MEMBER ( connection_t, tuple );
        break;
      default:
        size = 0;
    }

  return jhash8 ( key, size, 0 );
}

static int
ht_cb_compare ( const void *key1, const void *key2 )
{
  switch ( key_type )
    {
      case KEY_INODE:
        return ( *( unsigned long * ) key1 == *( unsigned long * ) key2 );
      case KEY_TUPLE:
        // print_tuple ( ( struct tuple * ) key1, ( struct tuple * ) key2 );
        return ( 0 == memcmp ( key1, key2, sizeof ( struct tuple ) ) );
    }

  return 0;
}

static void
ht_cb_free ( void *arg )
{
  connection_t *conn = arg;

  conn->use--;
  if ( !conn->use )
    free ( arg );
}

bool
connection_init ( void )
{
  // TODO: check free
  ht_connections = hashtable_new ( ht_cb_hash, ht_cb_compare, ht_cb_free );

  return ( NULL != ht_connections );
}

static connection_t *
create_new_conn ( unsigned long inode,
                  char *local_addr,
                  char *remote_addr,
                  uint16_t local_port,
                  uint16_t rem_port,
                  uint8_t state,
                  int protocol )
{
  // using calloc to ensure that struct tuple is clean
  connection_t *conn = calloc ( 1, sizeof ( connection_t ) );
  if ( !conn )
    {
      ERROR_DEBUG ( "\"%s\"", strerror ( errno ) );
      return NULL;
    }

  int rs = sscanf ( local_addr, "%x", &conn->tuple.l3.local.ip );

  if ( 1 != rs )
    goto EXIT_ERROR;

  rs = sscanf ( remote_addr, "%x", &conn->tuple.l3.remote.ip );

  if ( 1 != rs )
    goto EXIT_ERROR;

  conn->tuple.l4.local_port = local_port;
  conn->tuple.l4.remote_port = rem_port;
  conn->tuple.l4.protocol = protocol;
  conn->state = state;
  conn->inode = inode;

  conn->active = true;
  // memset ( &conn->net_stat, 0, sizeof ( struct net_stat ) );

  return conn;

EXIT_ERROR:
  ERROR_DEBUG ( "\"%s\"", strerror ( errno ) );
  free ( conn );
  return NULL;
}

static inline void
connection_insert_by_inode ( connection_t *conn )
{
  key_type = KEY_INODE;
  hashtable_set ( ht_connections, &conn->inode, conn );
}

static inline void
connection_insert_by_tuple ( connection_t *conn )
{
  key_type = KEY_TUPLE;
  hashtable_set ( ht_connections, &conn->tuple, conn );
}

static void
connection_insert ( connection_t *conn )
{
  conn->use = 2;  // tow references to conn on hashtable

  // make two entris in hashtable to same connection
  connection_insert_by_inode ( conn );
  connection_insert_by_tuple ( conn );
}

static int
connection_update_ ( const char *path_file, const int protocol )
{
  FILE *arq = fopen ( path_file, "r" );
  if ( !arq )
    {
      ERROR_DEBUG ( "\"%s\"", strerror ( errno ) );
      return 0;
    }

  int ret = 1;
  char *line = NULL;
  size_t len = 0;
  // ignore header in first line
  if ( ( getline ( &line, &len, arq ) ) == -1 )
    {
      ERROR_DEBUG ( "\"%s\"", strerror ( errno ) );
      ret = 0;
      goto EXIT;
    }

  while ( ( getline ( &line, &len, arq ) ) != -1 )
    {
      // clang-format off
      // sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode
      // 0: 3500007F:0035 00000000:0000 0A 00000000:00000000 00:00000000 00000000   101        0 20911 1 0000000000000000 100 0 0 10 0
      // 1: 0100007F:0277 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 44385 1 0000000000000000 100 0 0 10 0
      // 2: 0100007F:1733 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 27996 1 0000000000000000 100 0 0 10 0
      // clang-format on

      char local_addr[10], rem_addr[10];  // enough for ipv4
      unsigned long inode;
      uint16_t local_port, rem_port;
      uint8_t state;
      int rs = sscanf ( line,
                        "%*d: %9[0-9A-Fa-f]:%hX %9[0-9A-Fa-f]:%hX %hhX"
                        " %*X:%*X %*X:%*X %*X %*d %*d %lu %*512s\n",
                        local_addr,
                        &local_port,
                        rem_addr,
                        &rem_port,
                        &state,
                        &inode );

      if ( rs != 6 )
        {
          ERROR_DEBUG ( "Error read file conections\"%s\"",
                        strerror ( errno ) );
          ret = 0;
          goto EXIT;
        }

      // ignore this conections
      if ( state == TCP_TIME_WAIT || state == TCP_LISTEN )
        continue;

      // TODO: need check to tuple here? linux recycling inode?
      connection_t *conn = connection_get_by_inode ( inode );

      if ( conn )
        {
          conn->active = true;
          continue;
        }

      conn = create_new_conn ( inode,
                               local_addr,
                               rem_addr,
                               local_port,
                               rem_port,
                               state,
                               protocol );
      if ( !conn )
        {
          ret = 0;
          goto EXIT;
        }

      connection_insert ( conn );
    }

EXIT:
  free ( line );
  fclose ( arq );

  return ret;
}

static inline void
connection_remove_by_inode ( connection_t *conn )
{
  key_type = KEY_INODE;
  hashtable_remove ( ht_connections, &conn->inode );
}

static inline void
connection_remove_by_tuple ( connection_t *conn )
{
  key_type = KEY_TUPLE;
  hashtable_remove ( ht_connections, &conn->tuple );
}

static void
connection_remove ( connection_t *conn )
{
  connection_remove_by_inode ( conn );
  connection_remove_by_tuple ( conn );
  free ( conn );
}

static int
remove_dead_conn ( UNUSED hashtable_t *ht, void *value, UNUSED void *user_data )
{
  connection_t *conn = value;

  if ( !conn->active )
    {
      conn->use--;
      if ( !conn->use )
        connection_remove ( conn );
    }
  else
    conn->active = false;

  return 0;
}

#define PATH_TCP "/proc/net/tcp"
#define PATH_UDP "/proc/net/udp"

bool
connection_update ( const int proto )
{
  if ( proto & TCP )
    {
      if ( !connection_update_ ( PATH_TCP, IPPROTO_TCP ) )
        return 0;
    }

  if ( proto & UDP )
    {
      if ( !connection_update_ ( PATH_UDP, IPPROTO_UDP ) )
        return 0;
    }

  hashtable_foreach ( ht_connections, remove_dead_conn, NULL );
  return 1;
}

connection_t *
connection_get_by_inode ( const unsigned long inode )
{
  key_type = KEY_INODE;
  return hashtable_get ( ht_connections, &inode );
}

connection_t *
connection_get_by_tuple ( struct tuple *tuple )
{
  key_type = KEY_TUPLE;
  return hashtable_get ( ht_connections, tuple );
}

void
connection_free ( void )
{
  if ( ht_connections )
    hashtable_destroy ( ht_connections );
}
