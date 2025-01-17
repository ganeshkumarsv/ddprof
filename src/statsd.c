// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "statsd.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "ddres.h"

DDRes statsd_listen(const char *path, size_t sz_path, int *fd) {
  struct sockaddr_un addr_bind = {.sun_family = AF_UNIX};
  int fd_sock = -1;

  // Open the socket
  memcpy(addr_bind.sun_path, path, sz_path);
  int socktype = SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
  if (-1 == (fd_sock = socket(AF_UNIX, socktype, 0))) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "[STATSD] Creating UDS failed (%s)",
                          strerror(errno));
  }

  // Attempt to bind to the given path.  This is necessary for datagram-type
  // Unix domain sockets, since like UDP connections both the client and the
  // server need to be reachable on some resource.  TCP (and streaming UDS)
  // get that client-reachability satisfied at the protocol level, via the
  // assignment of ephemeral ports.  But for datagram-UDS/UDP we have to do
  // it ourselves.
  // In particular, that means if the user does not have the permission to
  // create and use a node somewhere on the VFS, then they cannot open a
  // listen-type datagram UDS.
  // TODO: is this true?  Can we relax this constraint?
  if (bind(fd_sock, (struct sockaddr *)&addr_bind, sizeof(addr_bind))) {
    close(fd_sock);
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Binding UDS failed (%s)",
                          strerror(errno));
  }

  *fd = fd_sock;
  return ddres_init();
}

DDRes statsd_connect(const char *path, size_t sz_path, int *fd) {
  assert(path);
  assert(sz_path);
  assert(fd);

  char path_listen[] = "/tmp/" MYNAME ".1234567890";
  size_t sz = 0;
  sz =
      snprintf(path_listen, sizeof(path_listen), "/tmp/" MYNAME "%d", getpid());
  struct sockaddr_un addr_peer = {.sun_family = AF_UNIX};
  int fd_sock = -1;

  memcpy(addr_peer.sun_path, path, sz_path);
  DDRes res = statsd_listen(path_listen, sz, &fd_sock);
  unlink(path_listen);
  if (IsDDResNotOK(res)) {
    return res;
  }

  // Now connect to the specified listening path
  if (connect(fd_sock, (struct sockaddr *)&addr_peer, sizeof(addr_peer))) {
    close(fd_sock);
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD,
                          "[STATSD] Connecting to host failed (%s)",
                          strerror(errno));
  }

  // If we're here, then the connection has been fully established
  *fd = fd_sock;
  return ddres_init();
}

DDRes statsd_send(int fd_sock, const char *key, void *val, int type) {
  char buf[1024] = {0};
  size_t sz = 0;
  switch (type) {
  default:
  case STAT_MS_LONG:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key, *(long *)val, "ms");
    break;
  case STAT_MS_FLOAT:
    sz = snprintf(buf, sizeof(buf), "%s:%f|%s", key, *(float *)val, "ms");
    break;
  case STAT_COUNT:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key, *(long *)val, "c");
    break;
  case STAT_GAUGE:
    sz = snprintf(buf, sizeof(buf), "%s:%ld|%s", key, *(long *)val, "g");
    break;
  }

  // Nothing to do if serialization failed or was short, but we don't return
  // granular result
  if (sz == 0 || sz >= sizeof(buf)) {
    // Not fatal
    DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Serialization failed");
  }

  // Nothing to do if the write fails
  while (sz != (size_t)write(fd_sock, buf, sz) && errno == EINTR) {
    // Don't consider this as fatal.
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Write failed (sys buffer full)");
    else
      DDRES_RETURN_WARN_LOG(DD_WHAT_STATSD, "Write failed");
  }
  return ddres_init();
}

DDRes statsd_close(int fd_sock) {
  DDRES_CHECK_INT(close(fd_sock), DD_WHAT_STATSD, "Error while closing socket");
  return ddres_init();
}
