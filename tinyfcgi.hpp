/*
 * tinyfcgi namespace contains tiny wrappers for FCGI_* structures

Synopsys

Client:

{
  // allocate buffer for message
  char buf[64 * 1024];

  {
    // construct tinyfcgi::message on buffer
    tinyfcgi::message m(1, buf, sizeof(buf));

    m.begin_request(FCGI_RESPONDER, FCGI_KEEP_CONN)                   // initialize request
      .add_param("GATEWAY_INTERFACE", "CGI/1.1")                      // add parameter
      .add_param("REQUEST_METHOD", "POST")                            //   ... method
      .add_param("CONTENT_TYPE", "application/x-www-form-urlencoded") //   ... content type
      .add_param("REQUEST_URI", "/test.cgi?v=1&type=test")            //   ... URI
      .add_param("HTTP_HOST", "localhost")                            //   ... HTTP host
      .append(FCGI_STDIN, "v=1&text=the+test")                        // append STDIN stream
      .end_stream(FCGI_STDIN);                                        // finalize stream

    send(sock, m.data(), m.size(), 0);                                // send out
  }

  ssize_t res = recv(sock, buf, sizeof(buf), 0);                      // let's assume we get all response
  {
    tinyfcgi::const_message m(buf, res);
    for(tinyfcgi::const_message::iterator i = m.begin();              // enum all headers
      i != m.end(); ++i) {
      const tinyfcgi::header& h = *i;
      assert(h.valid());                                              // it is required to validate header before using it
      assert(h.id() == 1);
      std::cout << "fcgi header " << h.type << "; size " << h.size();
      if (h.size() && (h.type == FCGI_STDOUT || h.type == FCGI_STDERR)) {
        std::cout << ": " << h.str();                                 // it is possible to merge chunks of same type with merge_next()
      }
      if (h.type == FCGI_END_REQUEST) {                               // response should be terminated with "END REQUEST"
        std::cout << "; status " << h.end_request()->app_status();
      }
      std::cout << std::endl;
    }
  }
}

Server:

{
  char buf[64 * 1024];                                                // allocate buffer for message
  uint16_t id;                                                        // store request id here

  ssize_t res = recv(sock, buf, sizeof(buf), 0);                      // let's assume we get all request
  {
    tinyfcgi::const_message m(buf, res);
    for(tinyfcgi::const_message::iterator i = m.begin();              // enum all headers
      i != m.end(); ++i) {
      const tinyfcgi::header& h = *i;
      assert(h.valid());                                              // it is required to validate header before using it
      std::cout << "fcgi header " << h.type << "; size " << h.size();
      if (h.type == FCGI_BEGIN_REQUEST) {
        std::cout << "; role " << h.begin_request()->role();
        id = h.id();
      }
      if (h.size() && h.type == FCGI_STDIN) {                         // request is complete, when it is terminated with empty STDIN chunk
        std::cout << ": " << h.str();
      }
      if (h.type == FCGI_PARAMS) {
        tinyfcgi::const_params params(h.str());
        for(tinyfcgi::const_params::iterator i = params.begin();      // enum all params
          i != params.end(); ++i) {
          const tinyfcgi::param& p = *i;
          string_ref name, value;
          p.read(name, value);
          std::cout << "  " << name << ": " << value << std::endl;
        }
      }
      std::cout << std::endl;
    }
  }

  {
    tinyfcgi::message m(id, buf, sizeof(buf));                        // create response in same buffer
    m.append(FCGI_STDOUT, "Status: 200\r\n")                          // fill it with data ...
      .append(FCGI_STDOUT, "Content-Type: text/plain\r\n")
      .append(FCGI_STDOUT, "Content-Length: 2\r\n\r\n")
      .append(FCGI_STDOUT, "Ok")
      .end_stream(FCGI_STDOUT)                                        // .. properly terminate ..
      .end_request(0, FCGI_REQUEST_COMPLETE);                         // .. and mark as complete

    send(sock, m.data(), m.size(), 0);                                // send back
  }
}

 */

// vim:ts=2:sts=2:sw=2:et
#pragma once

// all FCGI_* definitions are from here
#include "fastcgi.h"

#include <stdint.h>
#include <string.h>

#if HAVE_BOOST_STRING_REF
// this makes our life easier..
#include <boost/utility/string_ref.hpp>
#endif

namespace tinyfcgi {

#if HAVE_BOOST_STRING_REF
using boost::string_ref;
#endif

class begin_request_body : public FCGI_BeginRequestBody {
public:
  unsigned int role() const;
  void role(unsigned int r);
};


class end_request_body : public FCGI_EndRequestBody {
public:
  unsigned int app_status() const;
  void app_status(unsigned int s);
};


class header : public FCGI_Header {
private:
  header();
  header(const header&);
#if HAVE_MOVE_SEMANTIC
  header(header&&);
#endif

public:
  uint16_t size() const;
  void size(uint16_t s, uint8_t p);
  void size(uint16_t s);

  const char* data() const;
  char* data();

  string_ref str() const;
  void str(const string_ref& s);

  uint16_t id() const;
  void id(uint16_t r);

  const header* next() const;
  header* next();

  void merge_next();
  void append(const string_ref& str);

  bool valid() const;

  const begin_request_body* begin_request() const;
  const end_request_body* end_request() const;
};


class param {
private:
  param();
  param(const param&);
#if HAVE_MOVE_SEMANTIC
  param(param&&);
#endif

public:
  const param& read(string_ref& name, string_ref& value) const;
  param& write(const string_ref& name, const string_ref& value);

  const param* next() const;
  param* next();

  size_t size() const;
private:
  const param& read(size_t& s) const;
  const param& read(string_ref &s, size_t size) const;

  param& write(size_t s);
  param& write(const string_ref& s);

  const unsigned char* data() const { return (const unsigned char*)this; }
  unsigned char* data() { return (unsigned char*)this; }

  unsigned char dummy_[2];
};


class const_params {
public:
  class iterator {
  public:
    iterator(const char* buf = 0, const char* end = 0) :
      p_( (const param*)buf ), e_( (const param*)end ) { }

    const param* operator->() const { return p_; }
    const param& operator*() const { return *p_; }
    iterator& operator++() { p_ = p_->next(); return *this; }
    bool operator==(const iterator& a) const { return p_ == a.p_ || (not_valid() && a.not_valid()); }
    bool operator!=(const iterator& a) const { return p_ != a.p_ && (valid() || a.valid()); }
    bool operator<(const iterator& a) const { return p_ < a.p_; }
    bool operator<=(const iterator& a) const { return p_ <= a.p_; }

    bool not_valid() const { return p_ >= e_ || e_ - p_ < 1 || p_->next() > e_; }
    bool valid() const { return !not_valid(); }
  private:
    const param* p_;
    const param* e_;
  };

  const_params(const string_ref& buf) : buf_(buf) { }
  const_params(const char* buf, size_t size) : buf_(buf, size) { }

  iterator begin() const { return iterator(buf_.data(), buf_.data() + buf_.size()); }
  iterator end() const { return iterator(buf_.data() + buf_.size(), buf_.data() + buf_.size()); }

  const string_ref& str() const { return buf_; }
private:
  const string_ref buf_;
};


class const_message {
public:
  class iterator {
  public:
    iterator(const char* buf = 0, const char* end = 0) :
      h_( (const header*)buf ), e_( (const header*)end ) { }

    const header* operator->() const { return h_; }
    const header& operator*() const { return *h_; }
    iterator& operator++() { h_ = h_->next(); return *this; }
    bool operator==(const iterator& a) const { return h_ == a.h_ || (not_valid() && a.not_valid()); }
    bool operator!=(const iterator& a) const { return h_ != a.h_ && (valid() || a.valid()); }
    bool operator<(const iterator& a) const { return h_ < a.h_; }
    bool operator<=(const iterator& a) const { return h_ <= a.h_; }

    bool not_valid() const { return h_ >= e_ || e_ - h_ < 1 || h_->next() > e_; }
    bool valid() const { return !not_valid(); }
  private:
    const header* h_;
    const header* e_;
  };

  const_message(const string_ref& buf) : buf_(buf) { }
  const_message(const char* buf, size_t size) : buf_(buf, size) { }

  iterator begin() const { return iterator(buf_.data(), buf_.data() + buf_.size()); }
  iterator end() const { return iterator(buf_.data() + buf_.size(), buf_.data() + buf_.size()); }

  const string_ref& str() const { return buf_; }

private:
  const string_ref buf_;
};


class message {
public:
  message(uint16_t id, char* buf, size_t capacity);
  void clear();

  message& id(uint16_t id);

  message& begin_request(unsigned int role, unsigned char flags);
  message& end_request(unsigned int app_status, unsigned char proto_status);

  message& append(unsigned char type, const string_ref& str);
  message& end_stream(unsigned char type);

  message& add_param(const string_ref& name, const string_ref& value);

  const char* data() const;
  size_t size() const;
  const string_ref str() const;

  bool good() const;
  operator bool() const;

private:
  header* add_header(unsigned char type, bool force = false, size_t size = 0);
  char* terminator() const;
  void overflow();

private:
  uint16_t id_;
  char* buf_;
  size_t capacity_;
  header* cur_header_;
  bool good_;
  bool terminated_;
};


inline
uint16_t header::size() const {
  return (uint16_t)((contentLengthB1 << 8) + contentLengthB0);
}

inline
void header::size(uint16_t s, uint8_t p) {
  contentLengthB1 = s >> 8;
  contentLengthB0 = s & 0xFFu;
  paddingLength = p;
}

inline
void header::size(uint16_t s) {
  size(s, (8 - (s % 8)) % 8);
}

inline
const char* header::data() const {
  return (const char*)this + sizeof(FCGI_Header);
}

inline
char* header::data() {
  return (char*)this + sizeof(FCGI_Header);
}

inline
string_ref header::str() const {
  return string_ref(data(), size());
}

inline
void header::str(const string_ref& s) {
  memcpy(data(), s.data(), s.size());
  size(s.size());
}

inline
uint16_t header::id() const {
  return (uint16_t)((requestIdB1 << 8) + requestIdB0);
}

inline
void header::id(uint16_t r) {
  requestIdB1 = r >> 8;
  requestIdB0 = r & 0xFFu;
}

inline
const header* header::next() const {
  return (const header*)(data() + size() + paddingLength);
}

inline
header* header::next() {
  return (header*)(data() + size() + paddingLength);
}

inline
void header::merge_next() {
  uint16_t l = size();
  header* n = next();
  uint16_t nl = n->size();
  uint8_t np = paddingLength + n->paddingLength + sizeof(FCGI_Header);

  memmove(data() + l, n->data(), nl);
  size(l + nl, np);
}

inline
void header::append(const string_ref& str) {
  uint16_t s = size();
  memcpy(data() + s, str.data(), str.size());
  size(s + str.size());
}

inline
bool header::valid() const {
  return version == FCGI_VERSION_1 && type >= FCGI_BEGIN_REQUEST && type < FCGI_MAXTYPE;
}

inline
unsigned int
begin_request_body::role() const {
  return (unsigned int)((roleB1 << 8) + roleB0);
}

inline
void
begin_request_body::role(unsigned int r) {
  roleB1 = r >> 8;
  roleB0 = r & 0xFFu;
}


inline
unsigned int
end_request_body::app_status() const {
  return (unsigned int)((appStatusB3 << 24) + (appStatusB2 << 16) + (appStatusB1 << 8) + appStatusB0);
}

inline
void
end_request_body::app_status(unsigned int s) {
  appStatusB3 = s >> 24;
  appStatusB2 = s >> 16;
  appStatusB1 = s >> 8;
  appStatusB0 = s & 0xFFu;
}


inline
const param& param::read(string_ref& name, string_ref& value) const {
  size_t name_len = 0, value_len = 0;
  const param& p = read(name_len).read(value_len);
  return p.read(name, name_len)
      .read(value, value_len);
}

inline
param& param::write(const string_ref& name, const string_ref& value) {
  return
    write(name.size())
      .write(value.size())
      .write(name)
      .write(value);
}

inline
size_t param::size() const {
  size_t name_len = 0, value_len = 0;
  const unsigned char* start = data();
  const param& res = read(name_len).read(value_len);
  return res.data() - start + name_len + value_len;
}

inline
const param* 
param::next() const {
  const unsigned char* d = data() + size();
  return (const param*)d;
}

inline
param*
param::next() {
  unsigned char* d = data() + size();
  return (param*)d;
}

inline
const param& param::read(size_t& s) const {
  const unsigned char* d = data();
  if (d[0] >> 7) {
    s = ((d[0] & 0x7f) << 24) + (d[1] << 16) + (d[2] << 8) + d[3];
    d += 4;
  } else {
    s = d[0];
    d += 1;
  }
  return *((const param*)d);
}

inline
const param& param::read(string_ref &s, size_t size) const {
  const unsigned char* d = data();
  s = string_ref((const char*)d, size);
  d += size;
  return *((const param*)d);
}

inline
param& param::write(size_t s) {
  unsigned char* d = data();
  if (s <= 127) {
    d[0] = s & 0x7Fu;
    d += 1;
  } else {
    d[0] = (1u << 7) | (s >> 24);
    d[1] = s >> 16;
    d[2] = s >> 8;
    d[3] = s & 0xFFu;
    d += 4;
  }
  return *((param*)d);
}

inline
param& param::write(const string_ref& s) {
  unsigned char* d = data();
  memcpy(d, s.data(), s.size());
  d += s.size();
  return *((param*)d);
}


inline
message::message(uint16_t id, char* buf, size_t capacity) :
  id_(id), buf_(buf), capacity_(capacity), cur_header_( (header*)buf_ ),
  good_(capacity >= sizeof(FCGI_Header) + sizeof(FCGI_EndRequestBody)),
  terminated_(false) {
  cur_header_->type = 0;
}

inline
void message::clear() {
  cur_header_ = (header*)buf_;
  cur_header_->type = 0;
  good_ = capacity_ >= sizeof(FCGI_Header) + sizeof(FCGI_EndRequestBody);
  terminated_ = false;
}

inline
message& message::id(uint16_t id) {
  id_ = id;
  return *this;
}

inline
message& message::begin_request(unsigned int role, unsigned char flags) {
  header* h = add_header(FCGI_BEGIN_REQUEST, true, sizeof(FCGI_BeginRequestBody));
  if (h) {
    begin_request_body* res = (begin_request_body*)h->data();
    res->role(role);
    res->flags = flags;
  }
  return *this;
}

inline
message& message::end_request(unsigned int app_status, unsigned char proto_status) {
  header* h = add_header(FCGI_END_REQUEST, true, sizeof(FCGI_EndRequestBody));
  if (h) {
    end_request_body* res = (end_request_body*)h->data();
    res->app_status(app_status);
    res->protocolStatus = proto_status;
  }
  return *this;
}

inline
message& message::append(unsigned char type, const string_ref& str) {
  header* h = add_header(type);
  if (h) {
    if (h->data() + h->size() + str.size() > terminator()) {
      overflow();
    } else
      h->append(str);
  }
  return *this;
}

inline
message& message::end_stream(unsigned char type) {
  header* h = add_header(type);
  if (h && h->size()) h = add_header(type, true);
  if (h && type == FCGI_STDIN) terminated_ = true;
  return *this;
}

inline
message& message::add_param(const string_ref& name, const string_ref& value) {
  header* h = add_header(FCGI_PARAMS);
  if (h) {
    if (h->data() + h->size() + name.size() + value.size() > terminator()) {
      overflow();
    } else {
      param* p = (param*)(h->data() + h->size());
      p->write(name, value);
      h->size( h->size() + p->size() );
    }
  }
  return *this;
}

inline
const char* message::data() const {
  return buf_;
}

inline
size_t message::size() const {
  if (cur_header_->type) {
    return ((const char*)cur_header_->next()) - buf_;
  }
  return ((const char*)cur_header_) - buf_;
}

inline
const string_ref message::str() const {
  return string_ref(buf_, size());
}

inline
bool message::good() const {
  return good_;
}

inline
message::operator bool() const {
  return good_;
}

inline
header* message::add_header(unsigned char type, bool force, size_t size) {
  if (!good_) return 0;
  if (terminated_) {
    if (type == FCGI_END_REQUEST || type == FCGI_STDIN) return cur_header_;
    good_ = false;
    return 0;
  }
  if (cur_header_->type != type || force) {
    if (cur_header_->type) {
      header* n = cur_header_->next();
      if (type == FCGI_END_REQUEST || type == FCGI_STDIN) {
        header* t = (header*)(terminator());
        if (n > t) {
          good_ = false;
          return 0;
        }
      } else {
        header* t = (header*)(terminator() - sizeof(FCGI_Header) - size);
        if (n > t) {
          overflow();
          return 0;
        }
      }
      cur_header_ = n;
    }
    cur_header_->type = type;
    cur_header_->id(id_);
    cur_header_->version = FCGI_VERSION_1;
    cur_header_->size(size);
  }
  return cur_header_;
}

inline
char* message::terminator() const {
  header* h = (header*) buf_;
  char* res = buf_ + capacity_ - sizeof(FCGI_Header);
  if (h->type != FCGI_BEGIN_REQUEST) {
    res -= sizeof(FCGI_EndRequestBody);
  }
  return res;
}

inline
void message::overflow() {
  if (!good_) return;

  header* h = (header*) buf_;
  if (h->type == FCGI_BEGIN_REQUEST) {
    end_stream(FCGI_STDIN);
  } else {
    end_request(0, FCGI_OVERLOADED);
  }
  good_ = false;
}

}
