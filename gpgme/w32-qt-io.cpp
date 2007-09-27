/* w32-glib-io.c - W32 Glib I/O functions
   Copyright (C) 2000 Werner Koch (dd9jn)
   Copyright (C) 2001, 2002, 2004, 2005, 2007 g10 Code GmbH

   This file is part of GPGME.
 
   GPGME is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.
   
   GPGME is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.
   
   You should have received a copy of the GNU Lesser General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <windows.h>
#include <io.h>

#include "kdpipeiodevice.h"

extern "C"
{
#include "util.h"
#include "priv-io.h"
#include "sema.h"
#include "debug.h"
}

#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY	_O_BINARY
#else
#define O_BINARY	0
#endif
#endif

using _gpgme_::KDPipeIODevice;


/* This file is an ugly hack to get GPGME working with Qt on Windows
   targets.  On Windows, you can not select() on file descriptors.

   The only way to check if there is something to read is to read
   something.  This means that GPGME can not let Qt check for data
   without letting Qt also handle the data on Windows targets.

   The ugly consequence is that we need to work on QIODevices in
   GPGME, creating a Qt dependency.  Also, we need to export an
   interface for the application to get at GPGME's QIODevices.  There
   is no good way to abstract all this with callbacks, because the
   whole thing is also interconnected with the creation of pipes and
   child processes.

   The following rule applies only to this I/O backend:

   * ALL operations must use the user defined event loop.  GPGME can
   not anymore provide its own event loop.  This is mostly a sanity
   requirement: Although we have in theory all information we need to
   make the GPGME W32 code for select still work, it would be a big
   complication and require changes throughout GPGME.

   Eventually, we probably have to bite the bullet and make some
   really nice callback interfaces to let the user control all this at
   a per-context level.  */


#define MAX_SLAFD 256

QIODevice *iodevice_table[MAX_SLAFD];


static QIODevice *
find_channel (int fd, int create)
{
  if (fd < 0 || fd >= MAX_SLAFD)
    return NULL;

  if (create && !iodevice_table[fd])
    iodevice_table[fd] = new KDPipeIODevice
      (fd, QIODevice::ReadOnly|QIODevice::Unbuffered);

  return iodevice_table[fd];
}


/* Write the printable version of FD to the buffer BUF of length
   BUFLEN.  The printable version is the representation on the command
   line that the child process expects.  */
int
_gpgme_io_fd2str (char *buf, int buflen, int fd)
{
  return snprintf (buf, buflen, "%ld", (long) _get_osfhandle (fd));
}


void
_gpgme_io_subsystem_init (void)
{
}


static struct
{
  _gpgme_close_notify_handler_t handler;
  void *value;
} notify_table[MAX_SLAFD];


int
_gpgme_io_read (int fd, void *buffer, size_t count)
{
  int saved_errno = 0;
  qint64 nread;
  QIODevice *chan;
  TRACE_BEG2 (DEBUG_SYSIO, "_gpgme_io_read", fd,
	      "buffer=%p, count=%u", buffer, count);

  chan = find_channel (fd, 0);
  if (!chan)
    {
      TRACE_LOG ("no channel registered");
      errno = EINVAL;
      return TRACE_SYSRES (-1);
    }
  TRACE_LOG1 ("channel %p", chan);

  nread = chan->read ((char *) buffer, count);
  if (nread < 0)
    {
      TRACE_LOG1 ("err %s", qPrintable (chan->errorString ()));
      saved_errno = EIO;
      nread = -1;
    }

  TRACE_LOGBUF ((char *) buffer, nread);

  errno = saved_errno;
  return TRACE_SYSRES (nread);
}


int
_gpgme_io_write (int fd, const void *buffer, size_t count)
{
  qint64 nwritten;
  QIODevice *chan;
  TRACE_BEG2 (DEBUG_SYSIO, "_gpgme_io_write", fd,
	      "buffer=%p, count=%u", buffer, count);
  TRACE_LOGBUF ((char *) buffer, count);

  chan = find_channel (fd, 0);
  if (!chan)
    {
      TRACE_LOG ("fd %d: no channel registered");
      errno = EINVAL;
      return -1;
    }

  nwritten = chan->write ((char *) buffer, count);

  if (nwritten < 0)
    {
      nwritten = -1;
      errno = EIO;
      return TRACE_SYSRES(-1);
    }
  errno = 0;
  return TRACE_SYSRES (nwritten);
}


int
_gpgme_io_pipe (int filedes[2], int inherit_idx)
{
  QIODevice *chan;
  TRACE_BEG2 (DEBUG_SYSIO, "_gpgme_io_pipe", filedes,
	      "inherit_idx=%i (GPGME uses it for %s)",
	      inherit_idx, inherit_idx ? "reading" : "writing");

#define PIPEBUF_SIZE  4096
  if (_pipe (filedes, PIPEBUF_SIZE, O_NOINHERIT | O_BINARY) == -1)
    return TRACE_SYSRES (-1);

  /* Make one end inheritable. */
  if (inherit_idx == 0)
    {
      int new_read;

      new_read = _dup (filedes[0]);
      _close (filedes[0]);
      filedes[0] = new_read;

      if (new_read < 0)
	{
	  _close (filedes[1]);
	  return TRACE_SYSRES (-1);
	}
    }
  else if (inherit_idx == 1)
    {
      int new_write;

      new_write = _dup (filedes[1]);
      _close (filedes[1]);
      filedes[1] = new_write;

      if (new_write < 0)
	{
	  _close (filedes[0]);
	  return TRACE_SYSRES (-1);
	}
    }

  /* Now we have a pipe with the right end inheritable.  The other end
     should have a giochannel.  */
  chan = find_channel (filedes[1 - inherit_idx], 1);
  if (!chan)
    {
      int saved_errno = errno;
      _close (filedes[0]);
      _close (filedes[1]);
      errno = saved_errno;
      return TRACE_SYSRES (-1);
    }

  return TRACE_SUC5 ("read=0x%x/%p, write=0x%x/%p, channel=%p",
	  filedes[0], (HANDLE) _get_osfhandle (filedes[0]),
	  filedes[1], (HANDLE) _get_osfhandle (filedes[1]),
	  chan);
}


int
_gpgme_io_close (int fd)
{
  QIODevice *chan;
  TRACE_BEG (DEBUG_SYSIO, "_gpgme_io_close", fd);

  if (fd < 0 || fd >= MAX_SLAFD)
    {
      errno = EBADF;
      return TRACE_SYSRES (-1);
    }

  /* First call the notify handler.  */
  if (notify_table[fd].handler)
    {
      notify_table[fd].handler (fd, notify_table[fd].value);
      notify_table[fd].handler = NULL;
      notify_table[fd].value = NULL;
    }

  /* Then do the close.  */    
  chan = iodevice_table[fd];
  if (chan)
    {
      chan->close();
      delete chan;
      iodevice_table[fd] = NULL;
    }
  else
    _close (fd);

  return 0;
}


int
_gpgme_io_set_close_notify (int fd, _gpgme_close_notify_handler_t handler,
			    void *value)
{
  TRACE_BEG2 (DEBUG_SYSIO, "_gpgme_io_set_close_notify", fd,
	      "close_handler=%p/%p", handler, value);

  assert (fd != -1);

  if (fd < 0 || fd >= (int) DIM (notify_table))
    {
      errno = EINVAL;
      return TRACE_SYSRES (-1);
    }
  notify_table[fd].handler = handler;
  notify_table[fd].value = value;
  return TRACE_SYSRES (0);
}


int
_gpgme_io_set_nonblocking (int fd)
{
  /* Qt always uses non-blocking IO, except for files, maybe, but who
     uses that?  */
  TRACE_BEG (DEBUG_SYSIO, "_gpgme_io_set_nonblocking", fd);

  return TRACE_SYSRES (0);
}


static char *
build_commandline (char **argv)
{
  int i;
  int n = 0;
  char *buf;
  char *p;
  
  /* We have to quote some things because under Windows the program
     parses the commandline and does some unquoting.  We enclose the
     whole argument in double-quotes, and escape literal double-quotes
     as well as backslashes with a backslash.  We end up with a
     trailing space at the end of the line, but that is harmless.  */
  for (i = 0; argv[i]; i++)
    {
      p = argv[i];
      /* The leading double-quote.  */
      n++;
      while (*p)
	{
	  /* An extra one for each literal that must be escaped.  */
	  if (*p == '\\' || *p == '"')
	    n++;
	  n++;
	  p++;
	}
      /* The trailing double-quote and the delimiter.  */
      n += 2;
    }
  /* And a trailing zero.  */
  n++;

  buf = p = (char *) malloc (n);
  if (!buf)
    return NULL;
  for (i = 0; argv[i]; i++)
    {
      char *argvp = argv[i];

      *(p++) = '"';
      while (*argvp)
	{
	  if (*argvp == '\\' || *argvp == '"')
	    *(p++) = '\\';
	  *(p++) = *(argvp++);
	}
      *(p++) = '"';
      *(p++) = ' ';
    }
  *(p++) = 0;

  return buf;
}


int
_gpgme_io_spawn (const char *path, char **argv,
		 struct spawn_fd_item_s *fd_child_list,
		 struct spawn_fd_item_s *fd_parent_list)
{
  SECURITY_ATTRIBUTES sec_attr;
  PROCESS_INFORMATION pi =
    {
      NULL,      /* returns process handle */
      0,         /* returns primary thread handle */
      0,         /* returns pid */
      0         /* returns tid */
    };
  STARTUPINFO si;
  char *envblock = NULL;
  int cr_flags = CREATE_DEFAULT_ERROR_MODE
    | GetPriorityClass (GetCurrentProcess ());
  int i;
  char *arg_string;
  int duped_stdin = 0;
  int duped_stderr = 0;
  HANDLE hnul = INVALID_HANDLE_VALUE;
  /* FIXME.  */
  int debug_me = 0;
  TRACE_BEG1 (DEBUG_SYSIO, "_gpgme_io_spawn", path,
	      "path=%s", path);
  i = 0;
  while (argv[i])
    {
      TRACE_LOG2 ("argv[%2i] = %s", i, argv[i]);
      i++;
    }
  
  memset (&sec_attr, 0, sizeof sec_attr);
  sec_attr.nLength = sizeof sec_attr;
  sec_attr.bInheritHandle = FALSE;
  
  arg_string = build_commandline (argv);
  if (!arg_string)
    return TRACE_SYSRES (-1);
  
  memset (&si, 0, sizeof si);
  si.cb = sizeof (si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = debug_me? SW_SHOW : SW_HIDE;
  si.hStdInput = GetStdHandle (STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle (STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle (STD_ERROR_HANDLE);
  
  for (i = 0; fd_child_list[i].fd != -1; i++)
    {
      if (fd_child_list[i].dup_to == 0)
	{
	  si.hStdInput = (HANDLE) _get_osfhandle (fd_child_list[i].fd);
	  TRACE_LOG2 ("using 0x%x/%p for stdin", fd_child_list[i].fd,
		      _get_osfhandle (fd_child_list[i].fd));
	  duped_stdin = 1;
        }
      else if (fd_child_list[i].dup_to == 1)
	{
	  si.hStdOutput = (HANDLE) _get_osfhandle (fd_child_list[i].fd);
	  TRACE_LOG2 ("using 0x%x/%p for stdout", fd_child_list[i].fd,
		      _get_osfhandle (fd_child_list[i].fd));
	}
      else if (fd_child_list[i].dup_to == 2)
	{
	  si.hStdError = (HANDLE) _get_osfhandle (fd_child_list[i].fd);
	  TRACE_LOG2 ("using 0x%x/%p for stderr", fd_child_list[i].fd,
		      _get_osfhandle (fd_child_list[i].fd));
	  duped_stderr = 1;
        }
    }
  
  if (!duped_stdin || !duped_stderr)
    {
      SECURITY_ATTRIBUTES sa;
      
      memset (&sa, 0, sizeof sa);
      sa.nLength = sizeof sa;
      sa.bInheritHandle = TRUE;
      hnul = CreateFile ("nul",
			 GENERIC_READ|GENERIC_WRITE,
			 FILE_SHARE_READ|FILE_SHARE_WRITE,
			 &sa,
			 OPEN_EXISTING,
			 FILE_ATTRIBUTE_NORMAL,
			 NULL);
      if (hnul == INVALID_HANDLE_VALUE)
	{
	  TRACE_LOG1 ("CreateFile (\"nul\") failed: ec=%d",
		      (int) GetLastError ());
	  free (arg_string);
	  /* FIXME: Should translate the error code.  */
	  errno = EIO;
	  return TRACE_SYSRES (-1);
        }
      /* Make sure that the process has a connected stdin.  */
      if (!duped_stdin)
	{
	  si.hStdInput = hnul;
	  TRACE_LOG1 ("using 0x%x for dummy stdin", (int) hnul);
	}
      /* We normally don't want all the normal output.  */
      if (!duped_stderr)
	{
	  si.hStdError = hnul;
	  TRACE_LOG1 ("using %d for dummy stderr", (int)hnul);
	}
    }
  
  cr_flags |= CREATE_SUSPENDED;
  cr_flags |= DETACHED_PROCESS;
  if (!CreateProcessA (path,
		       arg_string,
		       &sec_attr,     /* process security attributes */
		       &sec_attr,     /* thread security attributes */
		       TRUE,          /* inherit handles */
		       cr_flags,      /* creation flags */
		       envblock,      /* environment */
		       NULL,          /* use current drive/directory */
		       &si,           /* startup information */
		       &pi))          /* returns process information */
    {
      TRACE_LOG1 ("CreateProcess failed: ec=%d", (int) GetLastError ());
      free (arg_string);
      /* FIXME: Should translate the error code.  */
      errno = EIO;
      return TRACE_SYSRES (-1);
    }
  
  /* Close the /dev/nul handle if used.  */
  if (hnul != INVALID_HANDLE_VALUE)
    {
      if (!CloseHandle (hnul))
	TRACE_LOG1 ("CloseHandle (hnul) failed: ec=%d (ignored)",
		    (int) GetLastError ());
    }
  
  /* Close the other ends of the pipes.  */
  for (i = 0; fd_parent_list[i].fd != -1; i++)
    _gpgme_io_close (fd_parent_list[i].fd);
  
  TRACE_LOG4 ("CreateProcess ready: hProcess=%p, hThread=%p, "
	      "dwProcessID=%d, dwThreadId=%d",
	      pi.hProcess, pi.hThread, 
	      (int) pi.dwProcessId, (int) pi.dwThreadId);
  
  if (ResumeThread (pi.hThread) < 0)
    TRACE_LOG1 ("ResumeThread failed: ec=%d", (int) GetLastError ());
  
  if (!CloseHandle (pi.hThread))
    TRACE_LOG1 ("CloseHandle of thread failed: ec=%d",
		(int) GetLastError ());

  TRACE_SUC1 ("process=%p", pi.hProcess);

  /* We don't need to wait for the process. */
  CloseHandle (pi.hProcess);

  return TRACE_SYSRES (0);
}


/* Select on the list of fds.  Returns: -1 = error, 0 = timeout or
   nothing to select, > 0 = number of signaled fds.  */
int
_gpgme_io_select (struct io_select_fd_s *fds, size_t nfds, int nonblock)
{
  int i;
  int count;
  /* Use a 1s timeout.  */

  void *dbg_help = NULL;
  TRACE_BEG2 (DEBUG_SYSIO, "_gpgme_io_select", fds,
	      "nfds=%u, nonblock=%u", nfds, nonblock);

  /* We only implement the special case of nonblock == true.  */
  assert (nonblock);

  count = 0;

  TRACE_SEQ (dbg_help, "select on [ ");
  for (i = 0; i < nfds; i++)
    {
      if (fds[i].fd == -1)
        {
          fds[i].signaled = 0;
        }
      else if (fds[i].frozen)
        {
          TRACE_ADD1 (dbg_help, "f0x%x ", fds[i].fd);
          fds[i].signaled = 0;
        }
      else if (fds[i].for_read )
        {
          const QIODevice * const chan = find_channel (fds[i].fd, 0);
          assert (chan);
          fds[i].signaled = chan->bytesAvailable() > 0 ? 1 : 0 ;
          TRACE_ADD1 (dbg_help, "w0x%x ", fds[i].fd);
          count++;
        }
      else if (fds[i].for_write)
        {
          const QIODevice * const chan = find_channel (fds[i].fd, 0);
          assert (chan);
          fds[i].signaled = chan->bytesToWrite() > 0 ? 0 : 1 ;
          TRACE_ADD1 (dbg_help, "w0x%x ", fds[i].fd);
          count++;
        }
    }
  TRACE_END (dbg_help, "]"); 

  return TRACE_SYSRES (count);
}


int
_gpgme_io_dup (int fd)
{
  return _dup (fd);
}


/* Look up the qiodevice for file descriptor FD.  */
extern "C"
void *
gpgme_get_fdptr (int fd)
{
  return find_channel (fd, 0);
}


/* Obsolete compatibility interface.  */
extern "C"
void *
gpgme_get_giochannel (int fd)
{
  return NULL;
}
