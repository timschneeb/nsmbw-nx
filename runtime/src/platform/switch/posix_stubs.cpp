// POSIX pieces Horizon/libnx lacks. Every caller already handles failure
// (transcript pipe disables itself, shell-open never runs, sqlite treats
// the syscall table defensively)
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int pipe(int fds[2]) {
    (void)fds;
    errno = ENOSYS;
    return -1;
}

pid_t waitpid(pid_t pid, int* status, int options) {
    (void)pid;
    (void)status;
    (void)options;
    errno = ENOSYS;
    return -1;
}

int execvp(const char* file, char* const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

int fchown(int fd, uid_t owner, gid_t group) {
    (void)fd;
    (void)owner;
    (void)group;
    errno = ENOSYS;
    return -1;
}

uid_t geteuid() { return 0; }
