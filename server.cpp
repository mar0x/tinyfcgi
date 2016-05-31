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

int process_conn(int sock) {
  while(true) {
    {
      // allocate buffer for message
      char buf[64 * 1024];
      size_t pos = 0;

      bool need_more(true);
      do {
        ssize_t r = read(sock, buf + pos, sizeof(buf) - pos);
        DEBUG("read(): " << r);
        if (r == 0) {
          std::cout << "connection #" << sock << " closed" << std::endl;
          return 0;
        }
        if (r == -1) {
          std::cerr << "read() failed: " << errno << std::endl;
          return 0;
        }
        pos += r;

        tinyfcgi::const_message m( string_ref(buf, pos) );
        for(tinyfcgi::const_message::iterator i = m.begin(); i != m.end(); ++i) {
          const tinyfcgi::header& h = *i;
          DEBUG("header: " << (unsigned int)h.type << "/" << h.size());
          if (!h.valid()) {
            std::cerr << "header is invalid" << std::endl;
            return 0;
          }
          if (h.type == FCGI_STDIN && h.size() == 0) {
            need_more = false;
          }
        }
      } while(need_more);

      tinyfcgi::const_message m( string_ref(buf, pos) );
      for(tinyfcgi::const_message::iterator i = m.begin(); i != m.end(); ++i) {
        DEBUG("header: " << (unsigned int)i->type << "/" << i->size());
        switch(i->type) {
        case FCGI_BEGIN_REQUEST:
          break;
        case FCGI_PARAMS: {
          tinyfcgi::const_params p(i->str());
          for(tinyfcgi::const_params::iterator pi = p.begin(); pi != p.end(); ++pi) {
            string_ref name, value;
            pi->read(name, value);
            DEBUG("  " << name << " = " << value);
          }
          break;
        }
        case FCGI_STDIN:
          DEBUG("STDIN: " << i->str());
          break;
        }
      }
    }

    {
      // allocate buffer for message
      char buf[64 * 1024];

      // construct tinyfcgi::message on buffer
      tinyfcgi::message m(1, buf, sizeof(buf));

      m.append(FCGI_STDOUT, "Status: 200 Oki-chpoki\r\nContent-Length: 4\r\n\r\nText")
        .end_stream(FCGI_STDOUT)
        .end_request(0, 200);

      DEBUG("m.size() = " << m.size());
      size_t s = m.size();
      size_t pos = 0;
      while(pos < s) {
        ssize_t res = send(sock, buf + pos, s - pos, 0);                 // send out
        DEBUG("send(): " << res);
        if (res == -1) {
          std::cerr << "send() failed: " << errno << std::endl;
          return 0;
        }
        pos += res;
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  const char* path = "sock";
  int backlog = 1024;

  DEBUG("__cplusplus = " << __cplusplus);
 
  int accept_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (accept_sock == -1) {
    std::cerr << "socket() failed: " << errno << std::endl;
    return 1;
  }

  sockaddr_un bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));

  bind_addr.sun_family = AF_UNIX;
  strncpy(bind_addr.sun_path, path, sizeof(bind_addr.sun_path) - 1);

  if (bind(accept_sock, (struct sockaddr *) &bind_addr,
         sizeof(bind_addr)) == -1)
  {
    std::cerr << "bind() failed: " << errno << std::endl;
    return 2;
  }

  if (listen(accept_sock, backlog) == -1)
  {
    std::cerr << "listen() failed: " << errno << std::endl;
    return 3;
  }

  while(true) {
    sockaddr_un peer_addr;
    socklen_t p_size = sizeof(peer_addr);
    int res = accept(accept_sock, (struct sockaddr *) &peer_addr, &p_size);
    if (res == -1)
    {
      std::cerr << "accept() failed: " << errno << std::endl;
      return 4;
    }
    process_conn(res);
    close(res);
  }
  return 0;
}
