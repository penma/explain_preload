This tool provides an `LD_PRELOAD`able library that enhances error output using libexplain.

libexplain tries to find out the reason for a failing system call and output that. Most programs do not use it, though. explain_preload is a preloadable library that modifies `strerror()` and related functions to output information from libexplain instead.

# Example
```
% ls /home/invalid/some/file 
ls: cannot access '/home/invalid/some/file': No such file or directory
% LD_PRELOAD=./expl.so ls /home/invalid/some/file  
ls: cannot access '/home/invalid/some/file': lstat(pathname = "/home/invalid/some/file",
data = 0x5643FC7F9C78) failed, No such file or directory (2, ENOENT) because there is no
"invalid" directory in the pathname "/home" directory
```

# How it works

A large number of system calls are wrapped so that if they fail, libexplain is invoked to record detailed information about why the call failed. This information is then provided to `strerror()`, `strerror_r()`, `error()`, instead of just the short conversion of the `errno` to a describing string.

There is no guarantee that the recorded information is actually for the same call which has last set `errno`. This can happen e.g. if some call is not supported by libexplain and hence not wrapped. It can also happen if the program invokes other syscalls before it generates the error message (although then even the original error output would have been misleading).

A basic sanity check is done: the message is only returned if the current value of `errno` matches the error number for which details were last recorded for.

Obviously the behaviour of the program changes quite a lot:
- `strerror()` returns different strings (duh), possibly much longer than what the program expects
- Small performance penalty on all supported syscalls because of the wrapping
- Medium performance penalty on failing syscalls because of libexplain having to figure out the reason, in case someone needs it
- Additional syscalls caused by libexplain's analysis (mostly probing file existence and `stat`s)
- Strange syscalls, in particular `lstat()` with nonsensical "filenames" (to check if an address is valid, libexplain uses `mincore()` if available, but sometimes falls back to the otherwise mostly side-effect-free syscall `lstat()`)

# Missing

Testing, testing, testing. The code is currently a fragile mess that is likely to crash programs in obscure ways due to all this wrapping. You have been warned.

Wrapping code for most calls is automatically generated. For some calls, this doesn't work well:
- `fcntl()`, `ioctl()`, `open()`, `openat()` because of variable parameter count
- `stat()`, `lstat()`, `fstat()` because [some people decided that they should go by the name `__xstat` instead](http://refspecs.linux-foundation.org/LSB_4.0.0/LSB-Core-generic/LSB-Core-generic/baselib---xstat.html)
- `pipe()` because of different API variants
- `ptrace()` because complexity hell (maybe in the future)
- `malloc()`, `free()`, `calloc()` because wrapping those makes the library even more fragile than it already is; also, you and the author of the program you want to run probably did not even know that these calls can set `errno`
- `wait*()` (reasons unclear, but wrapping them causes some programs to enter an infinite loop)

It would also be nice if the output of `strace` could be augmented with libexplain details. This may be implemented in a future version. (Extra work is needed since the error description is produced by `strace` itself but the analysis must be done from the perspective of the traced process.)
