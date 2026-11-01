/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003-2004 Theodore A. Roth <troth@openavr.org>
 * Copyright (C) 2006 Joerg Wunsch <j@uriah.heep.sax.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// Posix serial interface for avrdude

#if !defined(WIN32)
#include <ac_cfg.h>

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>

#include <fcntl.h>
#include <limits.h>
#include <termios.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <linux/usbdevice_fs.h>
#include <time.h>
#endif

#ifdef __APPLE__
#include <IOKit/serial/ioss.h>
#endif

#include "avrdude.h"
#include "libavrdude.h"

long serial_recv_timeout = 5000;        // ms
long serial_drain_timeout = 250;        // ms

struct baud_mapping {
  long baud;
  speed_t speed;
};

static const struct baud_mapping baud_lookup_table[] = {
  {300, B300},
  {600, B600},
  {1200, B1200},
  {2400, B2400},
  {4800, B4800},
  {9600, B9600},
  {19200, B19200},
  {38400, B38400},

#ifdef B57600
  {57600, B57600},
#endif

#ifdef B115200
  {115200, B115200},
#endif

#ifdef B230400
  {230400, B230400},
#endif

#ifdef B250000
  {250000, B250000},
#endif

#ifdef B460800
  {460800, B460800},
#endif

#ifdef B500000
  {500000, B500000},
#endif

#ifdef B576000
  {576000, B576000},
#endif

#ifdef B921600
  {921600, B921600},
#endif

#ifdef B1000000
  {1000000, B1000000},
#endif

#ifdef B1152000
  {1152000, B1152000},
#endif

#ifdef B1500000
  {1500000, B1500000},
#endif

#ifdef B2000000
  {2000000, B2000000},
#endif

#ifdef B2500000
  {2500000, B2500000},
#endif

#ifdef B3000000
  {3000000, B3000000},
#endif

#ifdef B3500000
  {3500000, B3500000},
#endif

#ifdef B4000000
  {4000000, B4000000},
#endif
  {0, 0}                        // Terminator
};

#ifdef __ANDROID__
static int android_usb_fd = -1;
static int android_usb_input_endpoint = -1;
static int android_usb_output_endpoint = -1;
static bool android_usb_ch340 = false;
static unsigned char android_usb_rx_buffer[4096];
static size_t android_usb_rx_offset = 0;
static size_t android_usb_rx_length = 0;

static int android_usb_control_out(int request, int value, int index) {
  struct usbdevfs_ctrltransfer transfer = {
    .bRequestType = 0x40,       // Vendor request, host to device
    .bRequest = request,
    .wValue = value,
    .wIndex = index,
    .wLength = 0,
    .timeout = 1000,
    .data = NULL
  };

  return ioctl(android_usb_fd, USBDEVFS_CONTROL, &transfer);
}

static int android_usb_set_dtr_rts(int is_on) {
  if(!android_usb_ch340)
    return 0;                   // Non-CH340 adapters are reset by Android before exec
  int lines = is_on? 0x60: 0;
  return android_usb_control_out(0xa4, (~lines) & 0xffff, 0);
}

static int android_usb_bulk_transfer(int endpoint, void *data, size_t length, unsigned int timeout) {
  struct usbdevfs_bulktransfer transfer = {
    .ep = (unsigned int) endpoint,
    .len = (unsigned int) length,
    .timeout = timeout,
    .data = data
  };
  int rc;

  do {
    rc = ioctl(android_usb_fd, USBDEVFS_BULK, &transfer);
  } while(rc < 0 && errno == EINTR);

  return rc;
}

static long long android_usb_now_ms(void) {
  struct timespec now;
  if(clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    return -1;
  return (long long) now.tv_sec*1000 + now.tv_nsec/1000000;
}

static int android_usb_parse_environment(const char *name, int max_value, int *value) {
  const char *text = getenv(name);
  char *end;
  long parsed;

  if(!text || !*text) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  parsed = strtol(text, &end, 10);
  if(errno || end == text || *end || parsed < 0 || parsed > max_value) {
    errno = EINVAL;
    return -1;
  }
  *value = (int) parsed;
  return 0;
}

static int android_usb_is_active(const union filedescriptor *fd) {
  return android_usb_fd >= 0 && fd->ifd == android_usb_fd;
}
#endif

static speed_t serial_baud_lookup(long baud, bool *nonstandard) {
  *nonstandard = false;

  for(const struct baud_mapping *map = baud_lookup_table; map->baud; map++)
    if(map->baud == baud)
      return map->speed;

  // Return the raw rate when asked for non-standard baud rate
  pmsg_notice2("%s(): using non-standard baud rate: %ld\n", __func__, baud);

  *nonstandard = true;
  return baud;
}

static int ser_setparams(const union filedescriptor *fd, long baud, unsigned long cflags) {
  int rc;
#ifdef __ANDROID__
  if(android_usb_is_active(fd))
    return 0;                   // Android configured the USB serial driver before exec
#endif
  struct termios termios;
  bool nonstandard;
  speed_t speed = serial_baud_lookup(baud, &nonstandard);

  if(!isatty(fd->ifd))
    return -ENOTTY;

  // Initialize terminal modes
  rc = tcgetattr(fd->ifd, &termios);
  if(rc < 0) {
    int ret = -errno;

    pmsg_ext_error("tcgetattr() failed\n");
    return ret;
  }

  // Copy termios for ser_close if we haven't already
  if(!cx->ser_saved_original_termios++)
    cx->ser_original_termios = termios;

  if(cflags & SERIAL_CREAD) {
    termios.c_cflag |= CREAD;
  }
  if(cflags & SERIAL_CLOCAL) {
    termios.c_cflag |= CLOCAL;
  }
  termios.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);

#ifdef ECHOCTL
  termios.c_lflag &= ~ECHOCTL;
#endif                          // ECHOCTL

#ifdef ECHOKE
  termios.c_lflag &= ~ECHOKE;
#endif                          // ECHOKE

  termios.c_oflag &= ~(OPOST | ONLCR | OCRNL);
  termios.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK);

#ifdef IUCLC
  termios.c_iflag &= ~IUCLC;
#endif                          // IUCLC

#ifdef PARMRK
  termios.c_iflag &= ~PARMRK;
#endif                          // PARMRK

  // MacOS doesn't handle nonstandard baudrate values in normal tcsetattr(), sigh
#ifdef __APPLE__
  if(!nonstandard) {
#endif

    cfsetospeed(&termios, speed);
    cfsetispeed(&termios, speed);

#ifdef __APPLE__
  }
#endif

  termios.c_cflag &= ~CSIZE;
  if(cflags & SERIAL_CS8) {
    termios.c_cflag |= CS8;
  }
  if(cflags & SERIAL_CS7) {
    termios.c_cflag |= CS7;
  }
  if(cflags & SERIAL_CS6) {
    termios.c_cflag |= CS6;
  }
  if(cflags & SERIAL_CS5) {
    termios.c_cflag |= CS5;
  }

  if(cflags & SERIAL_CSTOPB) {
    termios.c_cflag |= CSTOPB;
  } else {
    termios.c_cflag &= ~CSTOPB;
  }

  termios.c_iflag &= ~(INPCK | ISTRIP);

  if(cflags & (SERIAL_PARENB | SERIAL_PARODD)) {
    termios.c_cflag |= PARENB;
  } else {
    termios.c_cflag &= ~PARENB;
  }

  if(cflags & SERIAL_PARODD) {
    termios.c_cflag |= PARODD;
  } else {
    termios.c_cflag &= ~PARODD;
  }

#ifdef IXANY
  termios.c_iflag &= ~IXANY;
#endif                          // IXANY

  termios.c_iflag &= ~(IXON | IXOFF);

#ifdef CRTSCTS
  termios.c_iflag &= ~CRTSCTS;
#endif                          // CRTSCTS

#ifdef CNEW_RTSCTS
  termios.c_iflag &= ~CNEW_RTSCTS;
#endif                          // CRTSCTS

  rc = tcsetattr(fd->ifd, TCSANOW, &termios);
  if(rc < 0) {
    int ret = -errno;

    pmsg_ext_error("tcsetattr() failed\n");
    return ret;
  }

#ifdef __APPLE__
  // Handle nonstandard speed values the MacOS way
  if(nonstandard) {
    if(ioctl(fd->ifd, IOSSIOSPEED, &speed) < 0) {
      int ret = -errno;

      pmsg_ext_error("ioctrl(IOSSIOSPEED) failed\n");
      return ret;
    }
  }
#endif                          // __APPLE__

  tcflush(fd->ifd, TCIFLUSH);

  return 0;
}

/*
 * Given a port description of the form <host>:<port>, open a TCP connection to
 * the specified destination, which is assumed to be a terminal/console server
 * with serial parameters configured appropriately (e.g. 115200-8-N-1 for a
 * STK500.)
 */
static int net_open(const char *port, union filedescriptor *fdp) {
  char *hp, *hstr, *pstr;
  int s, fd, ret = -1;
  struct addrinfo hints;
  struct addrinfo *result, *rp;

  hstr = hp = mmt_strdup(port);

  /*
   * As numeric IPv6 addresses use colons as separators, we need to look for
   * the last colon here, which separates the port number or service name from
   * the host or IP address.
   */
  if(((pstr = strrchr(hstr, ':')) == NULL) || (pstr == hstr)) {
    pmsg_error("mangled host:port string %s\n", hstr);
    goto error;
  }

  // Remove brackets from the host part, if present
  if(*hstr == '[' && *(pstr - 1) == ']') {
    hstr++;
    *(pstr - 1) = '\0';
  }

  // Terminate the host section of the description
  *pstr++ = '\0';

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  s = getaddrinfo(hstr, pstr, &hints, &result);

  if(s != 0) {
    pmsg_ext_error("cannot resolve host=\"%s\", port=\"%s\": %s\n", hstr, pstr, gai_strerror(s));
    goto error;
  }
  for(rp = result; rp != NULL; rp = rp->ai_next) {
    fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if(fd == -1) {
      // This one failed, loop over
      continue;
    }
    if(connect(fd, rp->ai_addr, rp->ai_addrlen) != -1) {
      // Success, we are connected
      break;
    }
    close(fd);
  }
  if(rp == NULL) {
    pmsg_ext_error("cannot connect: %s\n", strerror(errno));
  } else {
    fdp->ifd = fd;
    ret = 0;
  }
  freeaddrinfo(result);

error:
  mmt_free(hp);
  return ret;
}

static int ser_set_dtr_rts(const union filedescriptor *fdp, int is_on) {
#ifdef __ANDROID__
  if(android_usb_is_active(fdp))
    return android_usb_set_dtr_rts(is_on);
#endif
  unsigned int ctl;
  int r;

  r = ioctl(fdp->ifd, TIOCMGET, &ctl);
  if(r < 0) {
    pmsg_ext_error("ioctl(\"TIOCMGET\"): %s\n", strerror(errno));
    return -1;
  }

  if(is_on) {
    // Set DTR and RTS
    ctl |= (TIOCM_DTR | TIOCM_RTS);
  } else {
    // Clear DTR and RTS
    ctl &= ~(TIOCM_DTR | TIOCM_RTS);
  }

  r = ioctl(fdp->ifd, TIOCMSET, &ctl);
  if(r < 0) {
    pmsg_ext_error("ioctl(\"TIOCMSET\"): %s\n", strerror(errno));
    return -1;
  }

  return 0;
}

static int ser_open(const char *port, union pinfo pinfo, union filedescriptor *fdp) {
  int rc;
  int fd;

#ifdef __ANDROID__
  if(strcmp(port, "COM") == 0) {
    const char *ch340 = getenv("ANDRUINO_USB_CH340");
    if(android_usb_fd >= 0 ||
       android_usb_parse_environment("ANDRUINO_USB_FD", INT_MAX, &fd) < 0 ||
       android_usb_parse_environment("ANDRUINO_USB_IN_EP", 255, &android_usb_input_endpoint) < 0 ||
       android_usb_parse_environment("ANDRUINO_USB_OUT_EP", 255, &android_usb_output_endpoint) < 0 ||
       !(android_usb_input_endpoint & 0x80) ||
       (android_usb_output_endpoint & 0x80) ||
       fcntl(fd, F_GETFD) < 0) {
      pmsg_ext_error("invalid inherited Android USB file descriptor or bulk endpoints\n");
      android_usb_fd = -1;
      return -1;
    }
    android_usb_fd = fd;
    fdp->ifd = fd;
    android_usb_ch340 = ch340 && strcmp(ch340, "1") == 0;
    android_usb_rx_offset = 0;
    android_usb_rx_length = 0;
    return 0;
  }
#endif

  /*
   * If the port is of the form "net:<host>:<port>", then handle it as a TCP
   * connection to a terminal server.
   */
  if(str_starts(port, "net:")) {
    return net_open(port + strlen("net:"), fdp);
  }

  // Open the serial port
  fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if(fd < 0) {
    pmsg_ext_error("cannot open port %s: %s\n", port, strerror(errno));
    return -1;
  }

  fdp->ifd = fd;

  // Set serial line attributes
  rc = ser_setparams(fdp, pinfo.serialinfo.baud, pinfo.serialinfo.cflags);
  if(rc) {
    pmsg_ext_error("cannot set attributes for port %s: %s\n", port, strerror(-rc));
    close(fd);
    return -1;
  }
  return 0;
}

static void ser_close(union filedescriptor *fd) {
#ifdef __ANDROID__
  if(android_usb_is_active(fd)) {
    android_usb_fd = -1;
    android_usb_input_endpoint = -1;
    android_usb_output_endpoint = -1;
    android_usb_ch340 = false;
    return;
  }
#endif
  // Restore original termios settings from ser_open
  if(cx->ser_saved_original_termios) {
    int rc = tcsetattr(fd->ifd, TCSANOW | TCSADRAIN, &cx->ser_original_termios);

    if(rc)
      pmsg_ext_error("cannot reset attributes for device: %s\n", strerror(errno));
    cx->ser_saved_original_termios = 0;
  }

  close(fd->ifd);
}

// Close but don't restore attributes
static void ser_rawclose(union filedescriptor *fd) {
#ifdef __ANDROID__
  if(android_usb_is_active(fd)) {
    android_usb_fd = -1;
    android_usb_input_endpoint = -1;
    android_usb_output_endpoint = -1;
    android_usb_ch340 = false;
    return;
  }
#endif
  cx->ser_saved_original_termios = 0;
  close(fd->ifd);
}

static int ser_send(const union filedescriptor *fd, const unsigned char *buf, size_t len) {
  int rc;

  trace_buffer(__func__, buf, len);

#ifdef __ANDROID__
  if(android_usb_is_active(fd)) {
    while(len) {
      rc = android_usb_bulk_transfer(android_usb_output_endpoint, (void *) buf,
                                     minm(len, 4096), 1000);
      if(rc < 0) {
        pmsg_ext_error("USB bulk write failed: %s\n", strerror(errno));
        return -1;
      }
      if(rc == 0) {
        pmsg_ext_error("USB bulk write returned no data\n");
        return -1;
      }
      buf += rc;
      len -= (size_t) rc;
    }
    return 0;
  }
#endif

  while(len) {
    rc = write(fd->ifd, buf, minm(len, 1024));
    if(rc < 0) {
      pmsg_ext_error("unable to write: %s\n", strerror(errno));
      return -1;
    }
    buf += rc;
    len -= rc;
  }

  return 0;
}

static int ser_recv(const union filedescriptor *fd, unsigned char *buf, size_t buflen) {
#ifdef __ANDROID__
  if(android_usb_is_active(fd)) {
    long long deadline = android_usb_now_ms() + serial_recv_timeout;
    size_t received = 0;

    while(received < buflen) {
      if(android_usb_rx_offset < android_usb_rx_length) {
        size_t available = android_usb_rx_length - android_usb_rx_offset;
        size_t needed = buflen - received;
        size_t count = minm(available, needed);
        memcpy(buf + received, android_usb_rx_buffer + android_usb_rx_offset, count);
        android_usb_rx_offset += count;
        received += count;
        if(android_usb_rx_offset == android_usb_rx_length)
          android_usb_rx_offset = android_usb_rx_length = 0;
        continue;
      }

      long long now = android_usb_now_ms();
      if(now < 0 || now >= deadline) {
        pmsg_notice2("%s(): programmer is not responding\n", __func__);
        return -1;
      }
      long long remaining = deadline - now;
      unsigned int timeout = remaining > UINT_MAX? UINT_MAX: (unsigned int) remaining;
      int rc = android_usb_bulk_transfer(android_usb_input_endpoint,
                                         android_usb_rx_buffer,
                                         sizeof(android_usb_rx_buffer), timeout);
      if(rc < 0) {
        if(errno == ETIMEDOUT || errno == EAGAIN) {
          pmsg_notice2("%s(): programmer is not responding\n", __func__);
        } else {
          pmsg_ext_error("USB bulk read failed: %s\n", strerror(errno));
        }
        return -1;
      }
      if(rc == 0) {
        pmsg_notice2("%s(): programmer returned no data\n", __func__);
        return -1;
      }
      android_usb_rx_offset = 0;
      android_usb_rx_length = (size_t) rc;
    }

    trace_buffer(__func__, buf, received);
    return 0;
  }
#endif
  struct timeval timeout, to2;
  fd_set rfds;
  int nfds;
  int rc;
  unsigned char *p = buf;
  size_t len = 0;

  timeout.tv_sec = serial_recv_timeout/1000L;
  timeout.tv_usec = (serial_recv_timeout%1000L)*1000;

  while(len < buflen) {
    to2 = timeout;
  reselect:
    FD_ZERO(&rfds);
    FD_SET(fd->ifd, &rfds);

    nfds = select(fd->ifd + 1, &rfds, NULL, NULL, &to2);
    if(nfds == 0) {
      pmsg_notice2("%s(): programmer is not responding\n", __func__);
      return -1;
    } else if(nfds == -1) {
      if(errno == EINTR || errno == EAGAIN) {
        pmsg_warning("programmer is not responding, reselecting\n");
        goto reselect;
      } else {
        pmsg_ext_error("select(): %s\n", strerror(errno));
        return -1;
      }
    }

    rc = read(fd->ifd, p, minm(buflen - len, 1024));
    if(rc < 0) {
      pmsg_ext_error("unable to read: %s\n", strerror(errno));
      return -1;
    }
    p += rc;
    len += rc;
  }

  trace_buffer(__func__, buf, len);
  return 0;
}

static int ser_drain(const union filedescriptor *fd, int display) {
#ifdef __ANDROID__
  if(android_usb_is_active(fd)) {
    unsigned char bytes[sizeof(android_usb_rx_buffer)];
    unsigned int timeout = (unsigned int) minm(serial_drain_timeout, UINT_MAX);

    if(display)
      msg_info("drain>");

    while(android_usb_rx_offset < android_usb_rx_length) {
      if(display)
        msg_info("%02x ", android_usb_rx_buffer[android_usb_rx_offset]);
      android_usb_rx_offset++;
    }
    android_usb_rx_offset = android_usb_rx_length = 0;

    while(1) {
      int rc = android_usb_bulk_transfer(android_usb_input_endpoint, bytes, sizeof(bytes), timeout);
      if(rc < 0) {
        if(errno == ETIMEDOUT || errno == EAGAIN)
          break;
        pmsg_ext_error("USB drain read failed: %s\n", strerror(errno));
        return -1;
      }
      if(rc == 0)
        break;
      if(display) {
        for(int i = 0; i < rc; i++)
          msg_info("%02x ", bytes[i]);
      }
    }

    if(display)
      msg_info("<drain\n");
    return 0;
  }
#endif
  struct timeval timeout;
  fd_set rfds;
  int nfds;
  int rc;
  unsigned char buf;

  timeout.tv_sec = 0;
  timeout.tv_usec = serial_drain_timeout*1000L;

  if(display) {
    msg_info("drain>");
  }

  while(1) {
    FD_ZERO(&rfds);
    FD_SET(fd->ifd, &rfds);

  reselect:
    nfds = select(fd->ifd + 1, &rfds, NULL, NULL, &timeout);
    if(nfds == 0) {
      if(display) {
        msg_info("<drain\n");
      }

      break;
    } else if(nfds == -1) {
      if(errno == EINTR) {
        goto reselect;
      } else {
        pmsg_ext_error("select(): %s\n", strerror(errno));
        return -1;
      }
    }

    rc = read(fd->ifd, &buf, 1);
    if(rc < 0) {
      pmsg_ext_error("unable to read: %s\n", strerror(errno));
      return -1;
    }
    if(display) {
      msg_info("%02x ", buf);
    }
  }

  return 0;
}

struct serial_device serial_serdev = {
  .open = ser_open,
  .setparams = ser_setparams,
  .close = ser_close,
  .rawclose = ser_rawclose,
  .send = ser_send,
  .recv = ser_recv,
  .drain = ser_drain,
  .set_dtr_rts = ser_set_dtr_rts,
  .flags = SERDEV_FL_CANSETSPEED,
};

struct serial_device *serdev = &serial_serdev;
#endif                          // WIN32
