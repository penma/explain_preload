#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <libexplain/libexplain.h>

static int (*real_open)(const char *pathname, int flags, mode_t mode) = NULL;
static int (*real_openat)(int dirfd, const char *pathname, int flags, mode_t mode) = NULL;
static ssize_t (*real_read)(int fd, void *buf, size_t count) = NULL;
static ssize_t (*real_write)(int fd, const void *buf, size_t count) = NULL;
static int (*real_rename)(const char *oldpath, const char *newpath) = NULL;
static int (*real_symlink)(const char *target, const char *linkpath) = NULL;
static int (*real_symlinkat)(const char *target, int newdirfd, const char *linkpath) = NULL;
static int (*real_execve)(const char *pathname, char *const argv[], char *const envp[]) = NULL;

static void (*real_error)(int status, int errnum, const char *format, ...) = NULL;
static char *(*real_strerror)(int errnum) = NULL;
static char *(*real_strerror_r)(int errnum, char *buf, size_t buflen) = NULL;

/* Recording of libexplain output */

#define RECORDED_MESSAGE_SIZE 8192
static char recorded_message[RECORDED_MESSAGE_SIZE];
static int recorded_errnum = 0; // if -1, then caller of ..._on_error should update with errno

static void tobuf_message(explain_output_t *op, const char *text) {
	(void)op;
	strncpy(recorded_message, text, RECORDED_MESSAGE_SIZE);
	recorded_message[RECORDED_MESSAGE_SIZE - 1] = '\0';
	recorded_errnum = -1;
}

static const explain_output_vtable_t vtable = {
	0, /* destructor */
	tobuf_message,
	0, /* exit */
	sizeof(explain_output_t)
};

static explain_output_t *tobuf_new(void) {
	return explain_output_new(&vtable);
}


static void resolve_real_functions(void) __attribute__((constructor));
static void resolve_real_functions(void) {
	real_open = dlsym(RTLD_NEXT, "open");
	if (real_open == NULL) {
		fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
	}
	// XXX needs proper error handling
	real_openat = dlsym(RTLD_NEXT, "openat");
	real_read = dlsym(RTLD_NEXT, "read");
	real_write = dlsym(RTLD_NEXT, "write");
	real_rename = dlsym(RTLD_NEXT, "rename");
	real_symlink = dlsym(RTLD_NEXT, "symlink");
	real_symlinkat = dlsym(RTLD_NEXT, "symlinkat");
	real_execve = dlsym(RTLD_NEXT, "execve");

	real_error = dlsym(RTLD_NEXT, "error");
	real_strerror = dlsym(RTLD_NEXT, "strerror");
	real_strerror_r = dlsym(RTLD_NEXT, "strerror_r");

	explain_output_register(tobuf_new());
	explain_program_name_assemble(0);
}

/**
 * Call this at the beginning of every wrapped syscall as follows:
 *   if (wrap_needed()) { ...code to wrap...; wrap_done(); } else { return real_call(...); }
 * The purpose is to prevent libexplain from using the wrapped versions
 */
static int in_wrappers = 0;

static int wrap_needed() {
	if (!in_wrappers) {
		in_wrappers = 1;
		return 1;
	} else {
		return 0;
	}
}

static void wrap_done() {
	if (recorded_errnum == -1) recorded_errnum = errno;
	in_wrappers = 0;
}


int open(const char *pathname, int flags, mode_t mode) {
	if (wrap_needed()) {
		int retval = explain_open_on_error(pathname, flags, mode);
		wrap_done();
		return retval;
	} else {
		return real_open(pathname, flags, mode);
	}
}

int openat(int dirfd, const char *pathname, int flags, mode_t mode) {
	if (wrap_needed()) {
		int retval = explain_openat_on_error(dirfd, pathname, flags, mode);
		wrap_done();
		return retval;
	} else {
		return real_openat(dirfd, pathname, flags, mode);
	}
}

ssize_t read(int fd, void *buf, size_t count) {
	if (wrap_needed()) {
		int retval = explain_read_on_error(fd, buf, count);
		wrap_done();
		return retval;
	} else {
		return real_read(fd, buf, count);
	}
}

ssize_t write(int fd, const void *buf, size_t count) {
	if (wrap_needed()) {
		int retval = explain_write_on_error(fd, buf, count);
		wrap_done();
		return retval;
	} else {
		return real_write(fd, buf, count);
	}
}

int rename(const char *oldpath, const char *newpath) {
	if (wrap_needed()) {
		int retval = explain_rename_on_error(oldpath, newpath);
		wrap_done();
		return retval;
	} else {
		return real_rename(oldpath, newpath);
	}
}

int symlink(const char *target, const char *linkpath) {
	if (wrap_needed()) {
		int retval = explain_symlink_on_error(target, linkpath);
		wrap_done();
		return retval;
	} else {
		return real_symlink(target, linkpath);
	}
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
	if (wrap_needed()) {
		int retval = real_symlinkat(target, newdirfd, linkpath);
		if (retval < 0) {
			recorded_errnum = errno;
			explain_message_errno_symlink(/* ! */ recorded_message, RECORDED_MESSAGE_SIZE, errno, target, linkpath);
			errno = recorded_errnum; // might have been eaten
		}
		wrap_done();
		return retval;
	} else {
		return real_symlinkat(target, newdirfd, linkpath);
	}
}

int execve(const char *pathname, char *const argv[], char *const envp[]) {
	if (wrap_needed()) {
		int retval = explain_execve_on_error(pathname, argv, envp);
		wrap_done();
		return retval;
	} else {
		return real_execve(pathname, argv, envp);
	}
}

/* XXX Overriding the error reporting functions */

char *our_strerror_r(int errnum, char *buf, size_t buflen) {
	// if the errnum doesn't match the recorded info, just format it
	if (errnum != recorded_errnum) {
		/* requested errnum doesn't match what we have recorded
		 * information for, so the recorded info is worthless.
		 * just convert code to string
		 */
		snprintf(buf, buflen, "%s (cause unknown)", real_strerror(errnum));
	} else {
		snprintf(buf, buflen, "%s", recorded_message);
	}
	return buf;
}

char *strerror_r(int errnum, char *buf, size_t buflen) {
	if (wrap_needed()) {
		char *retval = our_strerror_r(errnum, buf, buflen);
		wrap_done();
		return retval;
	} else {
		return real_strerror_r(errnum, buf, buflen);
	}
}

static char our_strerror_buf[1024];

char *strerror(int errnum) {
	if (wrap_needed()) {
		our_strerror_r(errno, our_strerror_buf, 1024);
		wrap_done();
		return our_strerror_buf;
	} else {
		return real_strerror(errnum);
	}
}


void error(int status, int errnum, const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	if (wrap_needed()) {
		extern unsigned int error_message_count;
		extern void (*error_print_progname)(void);

		fflush(stdout);
		if (error_print_progname == NULL) {
			fprintf(stderr, "%s: ", program_invocation_name);
		} else {
			error_print_progname(); // TODO test
		}
		vfprintf(stderr, format, ap);
		va_end(ap);
		++error_message_count;
		if (errnum) {
			char buf[1024];
			fprintf(stderr, ": %s", our_strerror_r(errnum, buf, 1024));
		} else {
			/* allowed by the interface of error(), however,
			 * more likely that we stomped on errno
			 */
			fprintf(stderr, " (errno clobbered)");
		}
		putc('\n', stderr);
		fflush(stderr);
		if (status) {
			exit(status);
		}
		wrap_done();
	} else {
		// ???? this shouldn't even happen...
		fflush(stdout);
		fprintf(stderr, "spurious call to error():\n");
		real_error(status, errnum, "(arguments omitted)");
		va_end(ap);
	}
}

/* // Uncomment to shorten explanation of search permission a bit
int explain_explain_search_permission(void *sb, const struct stat *st, const void *hip) {
return 0;
}
*/

