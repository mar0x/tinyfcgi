#include <iostream>
#define TRACE(x) std::cout << x << std::endl
#define DEBUG(x) std::cout << x << std::endl
#define INFO(x)  std::cout << x << std::endl
#define WARN(x)  std::cerr << x << std::endl
#define ERROR(x) std::cerr << x << std::endl

#define HAVE_BOOST_STRING_REF 1
#include "tinyfcgi.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>


using boost::string_ref;

int main(int argc, char** argv) {
  const char* path = "sock";
 
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock == -1) {
    std::cerr << "socket() failed: " << errno << std::endl;
    return 1;
  }

  if (argc > 1) {
    path = argv[1];
  }

  sockaddr_un connect_addr;
  memset(&connect_addr, 0, sizeof(connect_addr));

  connect_addr.sun_family = AF_UNIX;
  strncpy(connect_addr.sun_path, path, sizeof(connect_addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *) &connect_addr,
         sizeof(connect_addr)) == -1)
  {
    std::cerr << "connect() failed: " << errno << std::endl;
    return 2;
  }

  {
    // allocate buffer for message
    char buf[64 * 1024];

    // construct tinyfcgi::message on buffer
    tinyfcgi::message m(1, buf, sizeof(buf));

    m.begin_request(FCGI_RESPONDER, FCGI_KEEP_CONN)       // initialize request
      .add_param("TANYA", "1")                 // add parameter
      .add_param("PETYA", "2")                 //   .. one more
      .append(FCGI_STDIN, "Tanya + Petya");    // append STDIN stream

    m.append(FCGI_STDIN, " = ?")               // append more
      .end_stream(FCGI_STDIN);                 // finalize request

    DEBUG("m.size() = " << m.size());
    size_t s = m.size();
    size_t pos = 0;
    while(pos < s) {
      ssize_t res = send(sock, buf + pos, s - pos, 0);                 // send out
      DEBUG("send(): " << res);
      if (res == -1) {
        std::cerr << "send() failed: " << errno << std::endl;
        return 3;
      }
      pos += res;
    }
  }

  {
    // allocate buffer for message
    char buf[64 * 1024];
    size_t pos = 0;

    bool need_more(true);
    do {
      ssize_t r = read(sock, buf + pos, sizeof(buf) - pos);
      DEBUG("read(): " << r);
      if (r == -1) {
        std::cerr << "read() failed: " << errno << std::endl;
        return 4;
      }
      if (r == 0) {
        std::cerr << "read() connection closed by peer: " << errno << std::endl;
        return 5;
      }
      pos += r;

      tinyfcgi::const_message m( string_ref(buf, pos) );
      for(tinyfcgi::const_message::iterator i = m.begin(); i != m.end(); ++i) {
        DEBUG("header: " << (unsigned int)i->type << "/" << i->size());
        if (!i->valid()) {
          std::cerr << "header is invalid" << std::endl;
          return 5;
        }
        if (i->type == FCGI_END_REQUEST) {
          need_more = false;
        }
      }
    } while(need_more);

    tinyfcgi::const_message m( string_ref(buf, pos) );
    for(tinyfcgi::const_message::iterator i = m.begin(); i != m.end(); ++i) {
      std::cout << "type: " << (unsigned int)i->type << std::endl;
      std::cout << "size: " << i->size() << std::endl;
    }
  }

  return 0;
}
