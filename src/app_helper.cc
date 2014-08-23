/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2012 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>

#include <cassert>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <string>
#include <set>
#include <iomanip>
#include <fstream>

#include <zlib.h>

#include "app_helper.h"
#include "util.h"
#include "http2.h"

namespace nghttp2 {

namespace {
const char* strstatus(uint32_t error_code)
{
  switch(error_code) {
  case NGHTTP2_NO_ERROR:
    return "NO_ERROR";
  case NGHTTP2_PROTOCOL_ERROR:
    return "PROTOCOL_ERROR";
  case NGHTTP2_INTERNAL_ERROR:
    return "INTERNAL_ERROR";
  case NGHTTP2_FLOW_CONTROL_ERROR:
    return "FLOW_CONTROL_ERROR";
  case NGHTTP2_SETTINGS_TIMEOUT:
    return "SETTINGS_TIMEOUT";
  case NGHTTP2_STREAM_CLOSED:
    return "STREAM_CLOSED";
  case NGHTTP2_FRAME_SIZE_ERROR:
    return "FRAME_SIZE_ERROR";
  case NGHTTP2_REFUSED_STREAM:
    return "REFUSED_STREAM";
  case NGHTTP2_CANCEL:
    return "CANCEL";
  case NGHTTP2_COMPRESSION_ERROR:
    return "COMPRESSION_ERROR";
  case NGHTTP2_CONNECT_ERROR:
    return "CONNECT_ERROR";
  case NGHTTP2_ENHANCE_YOUR_CALM:
    return "ENHANCE_YOUR_CALM";
  case NGHTTP2_INADEQUATE_SECURITY:
    return "INADEQUATE_SECURITY";
  default:
    return "UNKNOWN";
  }
}
} // namespace

namespace {
const char* strsettingsid(int32_t id)
{
  switch(id) {
  case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
    return "SETTINGS_HEADER_TABLE_SIZE";
  case NGHTTP2_SETTINGS_ENABLE_PUSH:
    return "SETTINGS_ENABLE_PUSH";
  case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
    return "SETTINGS_MAX_CONCURRENT_STREAMS";
  case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
    return "SETTINGS_INITIAL_WINDOW_SIZE";
  default:
    return "UNKNOWN";
  }
}
} // namespace

namespace {
const char* strframetype(uint8_t type)
{
  switch(type) {
  case NGHTTP2_DATA:
    return "DATA";
  case NGHTTP2_HEADERS:
    return "HEADERS";
  case NGHTTP2_PRIORITY:
    return "PRIORITY";
  case NGHTTP2_RST_STREAM:
    return "RST_STREAM";
  case NGHTTP2_SETTINGS:
    return "SETTINGS";
  case NGHTTP2_PUSH_PROMISE:
    return "PUSH_PROMISE";
  case NGHTTP2_PING:
    return "PING";
  case NGHTTP2_GOAWAY:
    return "GOAWAY";
  case NGHTTP2_WINDOW_UPDATE:
    return "WINDOW_UPDATE";
  case NGHTTP2_EXT_ALTSVC:
    return "ALTSVC";
  default:
    return "UNKNOWN";
  }
};
} // namespace

namespace {
bool color_output = false;
} // namespace

void set_color_output(bool f)
{
  color_output = f;
}

namespace {
FILE *outfile = stdout;
} // namespace

void set_output(FILE *file)
{
  outfile = file;
}

namespace {
void print_frame_attr_indent()
{
  fprintf(outfile, "          ");
}
} // namespace

namespace {
const char* ansi_esc(const char *code)
{
  return color_output ? code : "";
}
} // namespace

namespace {
const char* ansi_escend()
{
  return color_output ? "\033[0m" : "";
}
} // namespace

namespace {
void print_nv(nghttp2_nv *nv)
{
  fprintf(outfile, "%s", ansi_esc("\033[1;34m"));
  fwrite(nv->name, nv->namelen, 1, outfile);
  fprintf(outfile, "%s: ", ansi_escend());
  fwrite(nv->value, nv->valuelen, 1, outfile);
  fprintf(outfile, "\n");
}
} // namespace
namespace {
void print_nv(nghttp2_nv *nva, size_t nvlen)
{
  auto end = nva + nvlen;
  for(; nva != end; ++nva) {
    print_frame_attr_indent();

    print_nv(nva);
  }
}
} // namelen

void print_timer()
{
  auto millis = get_timer();
  fprintf(outfile, "%s[%3ld.%03ld]%s",
          ansi_esc("\033[33m"),
          (long int)(millis.count()/1000), (long int)(millis.count()%1000),
          ansi_escend());
}

namespace {
void print_frame_hd(const nghttp2_frame_hd& hd)
{
  fprintf(outfile, "<length=%zu, flags=0x%02x, stream_id=%d>\n",
          hd.length, hd.flags, hd.stream_id);
}
} // namespace

namespace {
void print_flags(const nghttp2_frame_hd& hd)
{
  std::string s;
  switch(hd.type) {
  case NGHTTP2_DATA:
    if(hd.flags & NGHTTP2_FLAG_END_STREAM) {
      s += "END_STREAM";
    }
    if(hd.flags & NGHTTP2_FLAG_PADDED) {
      if(!s.empty()) {
        s += " | ";
      }
      s += "PADDED";
    }
    break;
  case NGHTTP2_HEADERS:
    if(hd.flags & NGHTTP2_FLAG_END_STREAM) {
      s += "END_STREAM";
    }
    if(hd.flags & NGHTTP2_FLAG_END_HEADERS) {
      if(!s.empty()) {
        s += " | ";
      }
      s += "END_HEADERS";
    }
    if(hd.flags & NGHTTP2_FLAG_PADDED) {
      if(!s.empty()) {
        s += " | ";
      }
      s += "PADDED";
    }
    if(hd.flags & NGHTTP2_FLAG_PRIORITY) {
      if(!s.empty()) {
        s += " | ";
      }
      s += "PRIORITY";
    }

    break;
  case NGHTTP2_PRIORITY:
    break;
  case NGHTTP2_SETTINGS:
    if(hd.flags & NGHTTP2_FLAG_ACK) {
      s += "ACK";
    }
    break;
  case NGHTTP2_PUSH_PROMISE:
    if(hd.flags & NGHTTP2_FLAG_END_HEADERS) {
      s += "END_HEADERS";
    }
    if(hd.flags & NGHTTP2_FLAG_PADDED) {
      if(!s.empty()) {
        s += " | ";
      }
      s += "PADDED";
    }
    break;
  case NGHTTP2_PING:
    if(hd.flags & NGHTTP2_FLAG_ACK) {
      s += "ACK";
    }
    break;
  }
  fprintf(outfile, "; %s\n", s.c_str());
}
} // namespace

enum print_type {
  PRINT_SEND,
  PRINT_RECV
};

namespace {
const char* frame_name_ansi_esc(print_type ptype)
{
  return ansi_esc(ptype == PRINT_SEND ? "\033[1;35m" : "\033[1;36m");
}
} // namespace

namespace {
void print_frame(print_type ptype, const nghttp2_frame *frame)
{
  fprintf(outfile, "%s%s%s frame ",
          frame_name_ansi_esc(ptype),
          strframetype(frame->hd.type),
          ansi_escend());
  print_frame_hd(frame->hd);
  if(frame->hd.flags) {
    print_frame_attr_indent();
    print_flags(frame->hd);
  }
  switch(frame->hd.type) {
  case NGHTTP2_DATA:
    if(frame->data.padlen > 0) {
      print_frame_attr_indent();
      fprintf(outfile, "(padlen=%zu)\n", frame->data.padlen);
    }
    break;
  case NGHTTP2_HEADERS:
    print_frame_attr_indent();
    fprintf(outfile, "(padlen=%zu", frame->headers.padlen);
    if(frame->hd.flags & NGHTTP2_FLAG_PRIORITY) {
      fprintf(outfile, ", stream_id=%d, weight=%u, exclusive=%d",
              frame->headers.pri_spec.stream_id,
              frame->headers.pri_spec.weight,
              frame->headers.pri_spec.exclusive);
    }
    fprintf(outfile, ")\n");
    switch(frame->headers.cat) {
    case NGHTTP2_HCAT_REQUEST:
      print_frame_attr_indent();
      fprintf(outfile, "; Open new stream\n");
      break;
    case NGHTTP2_HCAT_RESPONSE:
      print_frame_attr_indent();
      fprintf(outfile, "; First response header\n");
      break;
    case NGHTTP2_HCAT_PUSH_RESPONSE:
      print_frame_attr_indent();
      fprintf(outfile, "; First push response header\n");
      break;
    default:
      break;
    }
    print_nv(frame->headers.nva, frame->headers.nvlen);
    break;
  case NGHTTP2_PRIORITY:
    print_frame_attr_indent();

    fprintf(outfile, "(stream_id=%d, weight=%u, exclusive=%d)\n",
            frame->priority.pri_spec.stream_id,
            frame->priority.pri_spec.weight,
            frame->priority.pri_spec.exclusive);

    break;
  case NGHTTP2_RST_STREAM:
    print_frame_attr_indent();
    fprintf(outfile, "(error_code=%s(0x%02x))\n",
            strstatus(frame->rst_stream.error_code),
            frame->rst_stream.error_code);
    break;
  case NGHTTP2_SETTINGS:
    print_frame_attr_indent();
    fprintf(outfile, "(niv=%lu)\n",
            static_cast<unsigned long>(frame->settings.niv));
    for(size_t i = 0; i < frame->settings.niv; ++i) {
      print_frame_attr_indent();
      fprintf(outfile, "[%s(0x%02x):%u]\n",
              strsettingsid(frame->settings.iv[i].settings_id),
              frame->settings.iv[i].settings_id,
              frame->settings.iv[i].value);
    }
    break;
  case NGHTTP2_PUSH_PROMISE:
    print_frame_attr_indent();
    fprintf(outfile, "(padlen=%zu, promised_stream_id=%d)\n",
            frame->push_promise.padlen,
            frame->push_promise.promised_stream_id);
    print_nv(frame->push_promise.nva, frame->push_promise.nvlen);
    break;
  case NGHTTP2_PING:
    print_frame_attr_indent();
    fprintf(outfile, "(opaque_data=%s)\n",
            util::format_hex(frame->ping.opaque_data, 8).c_str());
    break;
  case NGHTTP2_GOAWAY:
    print_frame_attr_indent();
    fprintf(outfile,
            "(last_stream_id=%d, error_code=%s(0x%02x), "
            "opaque_data(%u)=[%s])\n",
            frame->goaway.last_stream_id,
            strstatus(frame->goaway.error_code),
            frame->goaway.error_code,
            static_cast<unsigned int>(frame->goaway.opaque_data_len),
            util::ascii_dump(frame->goaway.opaque_data,
                             frame->goaway.opaque_data_len).c_str());
    break;
  case NGHTTP2_WINDOW_UPDATE:
    print_frame_attr_indent();
    fprintf(outfile, "(window_size_increment=%d)\n",
            frame->window_update.window_size_increment);
    break;
  case NGHTTP2_EXT_ALTSVC: {
    print_frame_attr_indent();

    auto altsvc = static_cast<const nghttp2_ext_altsvc*>(frame->ext.payload);

    fprintf(outfile, "(max-age=%u, port=%u, protocol_id=",
            altsvc->max_age, altsvc->port);

    if(altsvc->protocol_id_len) {
      fwrite(altsvc->protocol_id, altsvc->protocol_id_len, 1, outfile);
    }

    fprintf(outfile, ", host=");

    if(altsvc->host_len) {
      fwrite(altsvc->host, altsvc->host_len, 1, outfile);
    }

    fprintf(outfile, ", origin=");

    if(altsvc->origin_len) {
      fwrite(altsvc->origin, altsvc->origin_len, 1, outfile);
    }

    fprintf(outfile, ")\n");

    break;
  }
  default:
    break;
  }
}
} // namespace

int verbose_on_header_callback(nghttp2_session *session,
                               const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags,
                               void *user_data)
{
  nghttp2_nv nv = {
    const_cast<uint8_t*>(name), const_cast<uint8_t*>(value),
    namelen, valuelen
  };

  print_timer();
  fprintf(outfile, " recv (stream_id=%d, noind=%d) ", frame->hd.stream_id,
          (flags & NGHTTP2_NV_FLAG_NO_INDEX) != 0);

  print_nv(&nv);
  fflush(outfile);

  return 0;
}

int verbose_on_frame_recv_callback
(nghttp2_session *session, const nghttp2_frame *frame, void *user_data)
{
  print_timer();
  fprintf(outfile, " recv ");
  print_frame(PRINT_RECV, frame);
  fflush(outfile);
  return 0;
}

int verbose_on_invalid_frame_recv_callback
(nghttp2_session *session, const nghttp2_frame *frame,
 uint32_t error_code, void *user_data)
{
  print_timer();
  fprintf(outfile, " [INVALID; status=%s] recv ", strstatus(error_code));
  print_frame(PRINT_RECV, frame);
  fflush(outfile);
  return 0;
}

int verbose_on_frame_send_callback
(nghttp2_session *session, const nghttp2_frame *frame, void *user_data)
{
  print_timer();
  fprintf(outfile, " send ");
  print_frame(PRINT_SEND, frame);
  fflush(outfile);
  return 0;
}

int verbose_on_data_chunk_recv_callback
(nghttp2_session *session, uint8_t flags, int32_t stream_id,
 const uint8_t *data, size_t len, void *user_data)
{
  print_timer();
  auto srecv = nghttp2_session_get_stream_effective_recv_data_length
    (session, stream_id);
  auto crecv = nghttp2_session_get_effective_recv_data_length(session);

  fprintf(outfile,
          " recv (stream_id=%d, length=%zu, srecv=%d, crecv=%d) DATA\n",
          stream_id, len, srecv, crecv);
  fflush(outfile);

  return 0;
}

namespace {
std::chrono::steady_clock::time_point base_tv;
} // namespace

void reset_timer()
{
  base_tv = std::chrono::steady_clock::now();
}

std::chrono::milliseconds get_timer()
{
  return time_delta(std::chrono::steady_clock::now(), base_tv);
}

std::chrono::steady_clock::time_point get_time()
{
  return std::chrono::steady_clock::now();
}

ssize_t deflate_data(uint8_t *out, size_t outlen,
                     const uint8_t *in, size_t inlen)
{
  int rv;
  z_stream zst;
  uint8_t temp_out[8192];
  auto temp_outlen = sizeof(temp_out);

  zst.next_in = Z_NULL;
  zst.zalloc = Z_NULL;
  zst.zfree = Z_NULL;
  zst.opaque = Z_NULL;

  rv = deflateInit2(&zst, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                    31, 9, Z_DEFAULT_STRATEGY);

  if(rv != Z_OK) {
    return -1;
  }

  zst.avail_in = inlen;
  zst.next_in = (uint8_t*)in;
  zst.avail_out = temp_outlen;
  zst.next_out = temp_out;

  rv = deflate(&zst, Z_FINISH);

  deflateEnd(&zst);

  if(rv != Z_STREAM_END) {
    return -1;
  }

  temp_outlen -= zst.avail_out;

  if(temp_outlen > outlen) {
    return -1;
  }

  memcpy(out, temp_out, temp_outlen);

  return temp_outlen;
}

} // namespace nghttp2
