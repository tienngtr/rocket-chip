// See LICENSE.Berkeley for license details.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "remote_bitbang.h"

/////////// remote_bitbang_t

remote_bitbang_t::remote_bitbang_t(uint16_t port) :
  socket_fd(0),
  client_fd(0),
  recv_cursor(0),
  recv_end(0),
  send_end(0)
{
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    fprintf(stderr, "remote_bitbang failed to make socket: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  fcntl(socket_fd, F_SETFL, O_NONBLOCK);
  int reuseaddr = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
                 sizeof(int)) == -1) {
    fprintf(stderr, "remote_bitbang failed setsockopt: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (::bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    fprintf(stderr, "remote_bitbang failed to bind socket: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  if (listen(socket_fd, 1) == -1) {
    fprintf(stderr, "remote_bitbang failed to listen on socket: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  socklen_t addrlen = sizeof(addr);
  if (getsockname(socket_fd, (struct sockaddr *) &addr, &addrlen) == -1) {
    fprintf(stderr, "remote_bitbang getsockname failed: %s (%d)\n",
            strerror(errno), errno);
    abort();
  }

  set_default_pins();

  fprintf(stderr, "This emulator compiled with JTAG Remote Bitbang client. To enable, use +jtag_rbb_enable=1.\n");
  fprintf(stderr, "Listening on port %d\n",
         ntohs(addr.sin_port));
}

void remote_bitbang_t::accept()
{

  fprintf(stderr,"Attempting to accept client socket\n");
  int again = 1;
  while (again != 0) {
    client_fd = ::accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
      if (errno == EAGAIN) {
        // No client waiting to connect right now.
      } else {
        fprintf(stderr, "failed to accept on socket: %s (%d)\n", strerror(errno),
                errno);
        again = 0;
        abort();
      }
    } else {
      fcntl(client_fd, F_SETFL, O_NONBLOCK);
      fprintf(stderr, "Accepted successfully.\n");
      again = 0;
    }
  }
}

void remote_bitbang_t::tick(
                            unsigned char * jtag_tck,
                            unsigned char * jtag_tms,
                            unsigned char * jtag_tdi,
                            unsigned char * jtag_trstn,
                            unsigned char jtag_tdo
                            )
{
  if (client_fd > 0) {
    tdo = jtag_tdo;
    execute_command();
  } else {
    this->accept();
  }

  * jtag_tck = tck;
  * jtag_tms = tms;
  * jtag_tdi = tdi;
  * jtag_trstn = trstn;

}

void remote_bitbang_t::set_pins(char _tck, char _tms, char _tdi){
  tck = _tck;
  tms = _tms;
  tdi = _tdi;
}

void remote_bitbang_t::set_default_pins() {
  set_pins(0, 1, 1);
  trstn = 1;
}

void remote_bitbang_t::flush_send_buf() {
  ssize_t send_cursor = 0;
  while (send_cursor != send_end) {
    ssize_t bytes = write(client_fd, send_buf + send_cursor, send_end - send_cursor);
    if (bytes == -1) {
      fprintf(stderr, "failed to write to socket: %s (%d)\n", strerror(errno), errno);
      abort();
    }
    send_cursor += bytes;
  }
  send_end = 0;
}

void remote_bitbang_t::read_into_recv_buf() {
  recv_cursor = 0;
  recv_end = read(client_fd, recv_buf, sizeof(recv_buf));
  if (recv_end == -1) {
    if (errno == EAGAIN) {
      // We'll try again the next call.
      //fprintf(stderr, "Received no command. Will try again on the next call\n");
      recv_end = recv_cursor;
    } else {
      fprintf(stderr, "remote_bitbang failed to read on socket: %s (%d)\n",
              strerror(errno), errno);
      abort();
    }
  } else if (recv_end == 0) {
    disconnect();
  }
}

void remote_bitbang_t::disconnect()
{
  set_default_pins();
  flush_send_buf();
  fprintf(stderr, "Remote end disconnected\n");
  close(client_fd);
  client_fd = 0;
}

void remote_bitbang_t::execute_command()
{
  while (recv_cursor == recv_end) {
    flush_send_buf();
    read_into_recv_buf();
  }

  //fprintf(stderr, "Received a command %c\n", command);

  char command = recv_buf[recv_cursor++];
  switch (command) {
  case 'B': /* fprintf(stderr, "*BLINK*\n"); */ break;
  case 'b': /* fprintf(stderr, "_______\n"); */ break;
  case 'r': case 's': trstn = 0; break; // This is not entirely true because we don't have SRST.
  case 't': case 'u': trstn = 1; break; // This is not entirely true because we don't have SRST.
  case '0': set_pins(0, 0, 0); break;
  case '1': set_pins(0, 0, 1); break;
  case '2': set_pins(0, 1, 0); break;
  case '3': set_pins(0, 1, 1); break;
  case '4': set_pins(1, 0, 0); break;
  case '5': set_pins(1, 0, 1); break;
  case '6': set_pins(1, 1, 0); break;
  case '7': set_pins(1, 1, 1); break;
  case 'R':
    send_buf[send_end++] = tdo ? '1' : '0';
    if (send_end == buf_size) {
      flush_send_buf();
    }
    break;
  case 'Q': disconnect(); break;
  default:
    fprintf(stderr, "remote_bitbang got unsupported command '%c'\n",
            command);
  }
}
