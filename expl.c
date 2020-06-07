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

/*
static void (*resolve_init_error(void))(int, int, const char *, ...) {
	return dlsym(RTLD_NEXT, "error");
}
static void INIT_error(int status, int errnum, const char *format, ...) __attribute__ ((ifunc("resolve_init_error")));
static char * (*resolve_init_strerror(void))(int) {
	return dlsym(RTLD_NEXT, "strerror");
}
static char *INIT_strerror(int errnum) __attribute__ ((ifunc("resolve_init_strerror")));
static char * (*resolve_init_strerror_r(void))(int, char *, size_t) {
	return dlsym(RTLD_NEXT, "strerror_r");
}
static char *INIT_strerror_r(int errnum, char *buf, size_t buflen) __attribute__ ((ifunc("resolve_init_strerror_r")));
*/
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void INIT_error(int status, int errnum, const char *format, ...) {
	// swallow it
}
static char *INIT_strerror(int errnum) {
	return "??";
}
static char *INIT_strerror_r(int errnum, char *buf, size_t buflen) {
	return "??";
}
#pragma GCC diagnostic pop

static void (*real_error)(int status, int errnum, const char *format, ...) = INIT_error;
static char *(*real_strerror)(int errnum) = INIT_strerror;
static char *(*real_strerror_r)(int errnum, char *buf, size_t buflen) = INIT_strerror_r;

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


char *REC_Buf() { return recorded_message; }


/**
 * Call this at the beginning of every wrapped syscall as follows:
 *   if (wrap_needed()) { ...code to wrap...; wrap_done(); } else { return real_call(...); }
 * The purpose is to prevent libexplain from using the wrapped versions
 */
static int enable_wrappers = 0; // no wrapping initially, otherwise startup code will try to use the wrappers even though they are not initialized yet

static int wrap_needed() {
	if (enable_wrappers) {
		enable_wrappers = 0;
		return 1;
	} else {
		return 0;
	}
}

static void wrap_done() {
	if (recorded_errnum == -1) recorded_errnum = errno;
	enable_wrappers = 1;
}

int explainpreload_wrap_enter() { return wrap_needed(); }
void explainpreload_wrap_leave() { wrap_done(); }

static void patch_error_functions(void) __attribute__((constructor));
static void patch_error_functions(void) {
/*
	real_open = dlsym(RTLD_NEXT, "open");
	if (real_open == NULL) {
		fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
	}
	// XXX needs proper error handling
*/

	real_error = dlsym(RTLD_NEXT, "error");
	real_strerror = dlsym(RTLD_NEXT, "strerror");
	real_strerror_r = dlsym(RTLD_NEXT, "strerror_r");

	explain_output_register(tobuf_new());
	explain_program_name_assemble(0);
	enable_wrappers = 1;
}

#if 0
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
#endif

int __lxstat64(int __ver, const char *__filename, struct stat64 *__stat_buf) {
	if (explainpreload_wrap_enter() && (__ver == _STAT_VER)) {
		int retval = explain_lstat_on_error(__filename, (struct stat *)__stat_buf); // FIXME: same type(?) on x86_64, check others
		explainpreload_wrap_leave();
		return retval;
	} else {
		// FIXME cache
		int (*f)(int, const char *, struct stat64 *);
		f = dlsym(RTLD_NEXT, "__lxstat64");
		return f(__ver, __filename, __stat_buf);
	}
}
int __lxstat(int __ver, const char *__filename, struct stat *__stat_buf) __attribute__ ((alias("__lxstat64")));

int __xstat64(int __ver, const char *__filename, struct stat64 *__stat_buf) {
	if (explainpreload_wrap_enter() && (__ver == _STAT_VER)) {
		int retval = explain_stat_on_error(__filename, (struct stat *)__stat_buf); // FIXME: same type(?) on x86_64, check others
		explainpreload_wrap_leave();
		return retval;
	} else {
		// FIXME cache
		int (*f)(int, const char *, struct stat64 *);
		f = dlsym(RTLD_NEXT, "__xstat64");
		return f(__ver, __filename, __stat_buf);
	}
}
int __xstat(int __ver, const char *__filename, struct stat *__stat_buf) __attribute__ ((alias("__xstat64")));

/* XXX Overriding the error reporting functions */

char *our_strerror_r(int errnum, char *buf, size_t buflen) {
	// if the errnum doesn't match the recorded info, just format it
	if (errnum != recorded_errnum) {
		/* requested errnum doesn't match what we have recorded
		 * information for, so the recorded info is worthless.
		 * just convert code to string
		 */
		snprintf(buf, buflen, "%s (cause unknown, recorded info: %s)", real_strerror(errnum), recorded_message);
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

#if 1
// Uncomment to shorten explanation of search permission a bit
// Note, returning 0 doesn't really conform to the API, but currently libexplain doesn't check the value so it doesn't matter
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
int explain_explain_search_permission(void *sb, const struct stat *st, const void *hip) {
	return 0;
}
int explain_explain_execute_permission(void *sb, const struct stat *st, const void *hip) {
	return 0;
}
#pragma GCC diagnostic pop

#endif

