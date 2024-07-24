// See LICENSE.Berkeley for license details.

#ifndef REMOTE_BITBANG_H
#define REMOTE_BITBANG_H

#include <stdint.h>
#include <sys/types.h>

class remote_bitbang_t
{
public:
  // Create a new server, listening for connections from localhost on the given
  // port.
  explicit remote_bitbang_t(uint16_t port);

  // Listen for connection on a Unix domain socket
  explicit remote_bitbang_t(const char* path);

  // Do a bit of work.
  void tick(unsigned char * jtag_tck,
            unsigned char * jtag_tms,
            unsigned char * jtag_tdi,
            unsigned char * jtag_trstn,
            unsigned char jtag_tdo);

  unsigned char done() {return 0;}
  
  int exit_code() {return 0;}
  
 private:
  unsigned char tck;
  unsigned char tms;
  unsigned char tdi;
  unsigned char trstn;
  unsigned char tdo;
    
  int socket_fd;
  int client_fd;

  static const ssize_t buf_size = 64 * 1024;
  char recv_buf[buf_size], send_buf[buf_size];
  ssize_t recv_cursor, recv_end, send_end;

  remote_bitbang_t();

  void flush_send_buf();
  void read_into_recv_buf();

  // Check for a client connecting, and accept if there is one.
  void accept();
  // Execute any commands the client has for us.
  // But we only execute 1 because we need time for the
  // simulation to run.
  void execute_command();

  void set_pins(char _tck, char _tms, char _tdi);
  void set_default_pins();
  void disconnect();
};

#endif
