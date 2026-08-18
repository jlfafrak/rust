//! Fixed-layout Rust bindings for the EOS v1 compatibility ABI.

use crate::prelude::*;

pub type size_t = usize;
pub type ssize_t = isize;
pub type ptrdiff_t = isize;
pub type intptr_t = isize;
pub type uintptr_t = usize;
pub type mode_t = u32;
pub type off_t = i64;
pub type time_t = i64;
pub type suseconds_t = i64;
pub type clockid_t = i32;
pub type socklen_t = u32;
pub type sa_family_t = u16;
pub type in_port_t = u16;
pub type in_addr_t = u32;
pub type nfds_t = u32;
pub type dev_t = u64;
pub type ino_t = u64;
pub type nlink_t = u32;
pub type uid_t = u32;
pub type gid_t = u32;
pub type blkcnt_t = u64;
pub type blksize_t = u32;
pub type pid_t = i32;
pub type pthread_t = u32;
pub type pthread_key_t = u32;
pub type eos_rust_process_t = u32;

s! {
    #[derive(Default)]
    pub struct timespec {
        pub tv_sec: time_t,
        pub tv_nsec: i64,
    }

    #[derive(Default)]
    pub struct timeval {
        pub tv_sec: time_t,
        pub tv_usec: suseconds_t,
    }

    pub struct stat {
        pub st_dev: dev_t,
        pub st_ino: ino_t,
        pub st_mode: mode_t,
        pub st_nlink: nlink_t,
        pub st_uid: uid_t,
        pub st_gid: gid_t,
        pub st_size: u64,
        pub st_atime: time_t,
        pub st_atime_nsec: i64,
        pub st_mtime: time_t,
        pub st_mtime_nsec: i64,
        pub st_ctime: time_t,
        pub st_ctime_nsec: i64,
        pub st_blocks: blkcnt_t,
        pub st_blksize: blksize_t,
        pub reserved: [u32; 7],
    }

    pub struct dirent {
        pub d_ino: ino_t,
        pub d_type: u32,
        pub d_name_length: u32,
        pub d_name: [c_char; 64],
    }

    pub struct iovec {
        pub iov_base: *mut c_void,
        pub iov_len: u32,
    }

    pub struct pthread_attr_t { pub words: [u32; 4], }
    pub struct pthread_mutex_t { pub words: [u32; 4], }
    pub struct pthread_mutexattr_t { pub words: [u32; 2], }
    pub struct pthread_cond_t { pub words: [u32; 4], }
    pub struct pthread_condattr_t { pub words: [u32; 2], }
    pub struct pthread_rwlock_t { pub words: [u32; 4], }
    pub struct pthread_once_t { pub words: [u32; 2], }

    pub struct in_addr { pub s_addr: in_addr_t, }
    pub struct in6_addr { pub s6_addr: [u8; 16], }
    pub struct sockaddr {
        pub sa_family: sa_family_t,
        pub sa_data: [u8; 14],
    }
    pub struct sockaddr_in {
        pub sin_family: sa_family_t,
        pub sin_port: in_port_t,
        pub sin_addr: in_addr,
        pub sin_zero: [u8; 8],
    }
    pub struct sockaddr_in6 {
        pub sin6_family: sa_family_t,
        pub sin6_port: in_port_t,
        pub sin6_flowinfo: u32,
        pub sin6_addr: in6_addr,
        pub sin6_scope_id: u32,
    }
    pub struct sockaddr_storage {
        pub ss_family: sa_family_t,
        pub ss_data: [u8; 26],
        pub ss_align: u32,
    }
    pub struct pollfd {
        pub fd: c_int,
        pub events: i16,
        pub revents: i16,
    }
    pub struct addrinfo {
        pub ai_flags: c_int,
        pub ai_family: c_int,
        pub ai_socktype: c_int,
        pub ai_protocol: c_int,
        pub ai_addrlen: socklen_t,
        pub ai_addr: *mut sockaddr,
        pub ai_canonname: *mut c_char,
        pub ai_next: *mut addrinfo,
    }

    pub struct linger {
        pub l_onoff: c_int,
        pub l_linger: c_int,
    }
    pub struct ip_mreq {
        pub imr_multiaddr: in_addr,
        pub imr_interface: in_addr,
    }
    pub struct ipv6_mreq {
        pub ipv6mr_multiaddr: in6_addr,
        pub ipv6mr_interface: u32,
    }

    pub struct eos_rust_spawn_request {
        pub program: *const c_char,
        pub argv: *const *const c_char,
        pub argc: u32,
        pub envp: *const *const c_char,
        pub envc: u32,
        pub cwd: *const c_char,
        pub stdin_fd: c_int,
        pub stdout_fd: c_int,
        pub stderr_fd: c_int,
        pub flags: u32,
        pub reserved: [u32; 7],
    }
    pub struct eos_rust_process_status {
        pub kind: u32,
        pub code: i32,
        pub reserved: [u32; 6],
    }
}

pub const EXIT_SUCCESS: c_int = 0;
pub const EXIT_FAILURE: c_int = 1;
pub const EOS_RUST_PROCESS_EXITED: u32 = 1;
pub const EOS_RUST_PROCESS_TERMINATED: u32 = 2;

pub const ENOENT: c_int = 2;
pub const ESRCH: c_int = 3;
pub const EINTR: c_int = 4;
pub const EIO: c_int = 5;
pub const EBADF: c_int = 9;
pub const ECHILD: c_int = 10;
pub const EDEADLK: c_int = 11;
pub const ENOMEM: c_int = 12;
pub const EACCES: c_int = 13;
pub const EPERM: c_int = EACCES;
pub const EFAULT: c_int = 14;
pub const EBUSY: c_int = 16;
pub const EEXIST: c_int = 17;
pub const ENOTDIR: c_int = 20;
pub const EISDIR: c_int = 21;
pub const EINVAL: c_int = 22;
pub const EMFILE: c_int = 24;
pub const ENOTTY: c_int = 25;
pub const ENOSPC: c_int = 28;
pub const ESPIPE: c_int = 29;
pub const EROFS: c_int = 30;
pub const EPIPE: c_int = 32;
pub const ERANGE: c_int = 34;
pub const EAGAIN: c_int = 35;
pub const EWOULDBLOCK: c_int = EAGAIN;
pub const EINPROGRESS: c_int = 36;
pub const EALREADY: c_int = 37;
pub const ENOTSOCK: c_int = 38;
pub const EDESTADDRREQ: c_int = 39;
pub const EMSGSIZE: c_int = 40;
pub const EPROTOTYPE: c_int = 41;
pub const ENOPROTOOPT: c_int = 42;
pub const EPROTONOSUPPORT: c_int = 43;
pub const ENOTSUP: c_int = 45;
pub const EOPNOTSUPP: c_int = ENOTSUP;
pub const ENOSYS: c_int = ENOTSUP;
pub const EAFNOSUPPORT: c_int = 47;
pub const EADDRINUSE: c_int = 48;
pub const EADDRNOTAVAIL: c_int = 49;
pub const ENETDOWN: c_int = 50;
pub const ENETUNREACH: c_int = 51;
pub const ENETRESET: c_int = 52;
pub const ECONNABORTED: c_int = 53;
pub const ECONNRESET: c_int = 54;
pub const ENOBUFS: c_int = 55;
pub const EISCONN: c_int = 56;
pub const ENOTCONN: c_int = 57;
pub const ESHUTDOWN: c_int = 58;
pub const ETIMEDOUT: c_int = 60;
pub const ECONNREFUSED: c_int = 61;
pub const ENAMETOOLONG: c_int = 63;
pub const EHOSTDOWN: c_int = 64;
pub const EHOSTUNREACH: c_int = 65;
pub const ENOTEMPTY: c_int = 66;
pub const EOVERFLOW: c_int = 84;

pub const O_RDONLY: c_int = 0;
pub const O_WRONLY: c_int = 1;
pub const O_RDWR: c_int = 2;
pub const O_ACCMODE: c_int = 3;
pub const O_CREAT: c_int = 0x40;
pub const O_EXCL: c_int = 0x80;
pub const O_TRUNC: c_int = 0x200;
pub const O_APPEND: c_int = 0x400;
pub const O_NONBLOCK: c_int = 0x800;
pub const O_CLOEXEC: c_int = 0x80000;
pub const F_DUPFD: c_int = 0;
pub const F_GETFD: c_int = 1;
pub const F_SETFD: c_int = 2;
pub const F_GETFL: c_int = 3;
pub const F_SETFL: c_int = 4;
pub const F_DUPFD_CLOEXEC: c_int = 1030;
pub const FD_CLOEXEC: c_int = 1;
pub const SEEK_SET: c_int = 0;
pub const SEEK_CUR: c_int = 1;
pub const SEEK_END: c_int = 2;

pub const S_IFMT: mode_t = 0o170000;
pub const S_IFSOCK: mode_t = 0o140000;
pub const S_IFLNK: mode_t = 0o120000;
pub const S_IFREG: mode_t = 0o100000;
pub const S_IFBLK: mode_t = 0o060000;
pub const S_IFDIR: mode_t = 0o040000;
pub const S_IFCHR: mode_t = 0o020000;
pub const S_IFIFO: mode_t = 0o010000;
pub const S_ISUID: mode_t = 0o004000;
pub const S_ISGID: mode_t = 0o002000;
pub const S_ISVTX: mode_t = 0o001000;
pub const S_IRUSR: mode_t = 0o000400;
pub const S_IWUSR: mode_t = 0o000200;
pub const S_IXUSR: mode_t = 0o000100;
pub const S_IRGRP: mode_t = 0o000040;
pub const S_IWGRP: mode_t = 0o000020;
pub const S_IXGRP: mode_t = 0o000010;
pub const S_IROTH: mode_t = 0o000004;
pub const S_IWOTH: mode_t = 0o000002;
pub const S_IXOTH: mode_t = 0o000001;

pub const PATH_MAX: c_int = 256;
pub const NAME_MAX: c_int = 63;
pub const DT_UNKNOWN: u32 = 0;
pub const DT_DIR: u32 = 4;
pub const DT_REG: u32 = 8;
pub const DT_FIFO: u32 = 1;
pub const DT_CHR: u32 = 2;
pub const DT_BLK: u32 = 6;
pub const DT_LNK: u32 = 10;
pub const DT_SOCK: u32 = 12;
pub const IOV_MAX: c_int = 1024;
pub const UIO_MAXIOV: c_int = IOV_MAX;

pub const PTHREAD_STACK_MIN: usize = 4096;
pub const PTHREAD_MUTEX_NORMAL: c_int = 0;
pub const PTHREAD_MUTEX_RECURSIVE: c_int = 1;
pub const PTHREAD_MUTEX_INITIALIZER: pthread_mutex_t = pthread_mutex_t { words: [0; 4] };
pub const PTHREAD_COND_INITIALIZER: pthread_cond_t = pthread_cond_t { words: [0; 4] };
pub const PTHREAD_ONCE_INIT: pthread_once_t = pthread_once_t { words: [0; 2] };
pub const CLOCK_REALTIME: clockid_t = 0;
pub const CLOCK_MONOTONIC: clockid_t = 1;

pub const AF_UNSPEC: c_int = 0;
pub const AF_INET: c_int = 2;
pub const AF_INET6: c_int = 10;
pub const SOCK_STREAM: c_int = 1;
pub const SOCK_DGRAM: c_int = 2;
pub const IPPROTO_IP: c_int = 0;
pub const IPPROTO_TCP: c_int = 6;
pub const IPPROTO_UDP: c_int = 17;
pub const IPPROTO_IPV6: c_int = 41;
pub const SHUT_RD: c_int = 0;
pub const SHUT_WR: c_int = 1;
pub const SHUT_RDWR: c_int = 2;
pub const MSG_OOB: c_int = 0x1;
pub const MSG_PEEK: c_int = 0x2;
pub const MSG_DONTROUTE: c_int = 0x4;
pub const MSG_DONTWAIT: c_int = 0x40;
pub const MSG_NOSIGNAL: c_int = 0x4000;
pub const SOL_SOCKET: c_int = 1;
pub const SO_REUSEADDR: c_int = 2;
pub const SO_ERROR: c_int = 4;
pub const SO_BROADCAST: c_int = 6;
pub const SO_SNDBUF: c_int = 7;
pub const SO_RCVBUF: c_int = 8;
pub const SO_KEEPALIVE: c_int = 9;
pub const SO_LINGER: c_int = 13;
pub const SO_RCVTIMEO: c_int = 20;
pub const SO_SNDTIMEO: c_int = 21;
pub const TCP_NODELAY: c_int = 1;
pub const IP_TTL: c_int = 2;
pub const IP_MULTICAST_TTL: c_int = 33;
pub const IP_MULTICAST_LOOP: c_int = 34;
pub const IP_ADD_MEMBERSHIP: c_int = 35;
pub const IP_DROP_MEMBERSHIP: c_int = 36;
pub const IPV6_MULTICAST_LOOP: c_int = 19;
pub const IPV6_ADD_MEMBERSHIP: c_int = 20;
pub const IPV6_DROP_MEMBERSHIP: c_int = 21;
pub const IPV6_V6ONLY: c_int = 26;
pub const SOMAXCONN: c_int = 128;

pub const POLLIN: i16 = 0x001;
pub const POLLPRI: i16 = 0x002;
pub const POLLOUT: i16 = 0x004;
pub const POLLERR: i16 = 0x008;
pub const POLLHUP: i16 = 0x010;
pub const POLLNVAL: i16 = 0x020;

pub const AI_PASSIVE: c_int = 0x001;
pub const AI_CANONNAME: c_int = 0x002;
pub const AI_NUMERICHOST: c_int = 0x004;
pub const AI_V4MAPPED: c_int = 0x008;
pub const AI_ALL: c_int = 0x010;
pub const AI_ADDRCONFIG: c_int = 0x020;
pub const AI_NUMERICSERV: c_int = 0x400;
pub const EAI_BADFLAGS: c_int = -1;
pub const EAI_NONAME: c_int = -2;
pub const EAI_AGAIN: c_int = -3;
pub const EAI_FAIL: c_int = -4;
pub const EAI_FAMILY: c_int = -6;
pub const EAI_SOCKTYPE: c_int = -7;
pub const EAI_SERVICE: c_int = -8;
pub const EAI_MEMORY: c_int = -10;
pub const EAI_SYSTEM: c_int = -11;
pub const EAI_OVERFLOW: c_int = -12;

unsafe extern "C" {
    #[link_name = "eos_rust_abi_version"]
    pub fn eos_abi_version() -> u32;
    #[link_name = "eos_rust_abi_require"]
    pub fn eos_abi_require(major: u32, minimum_minor: u32) -> i32;
    #[link_name = "eos_rust_errno_location"]
    pub fn __errno() -> *mut c_int;

    #[link_name = "eos_rust_malloc"]
    pub fn malloc(byte_count: u32) -> *mut c_void;
    #[link_name = "eos_rust_calloc"]
    pub fn calloc(element_count: u32, element_size: u32) -> *mut c_void;
    #[link_name = "eos_rust_realloc"]
    pub fn realloc(memory: *mut c_void, byte_count: u32) -> *mut c_void;
    #[link_name = "eos_rust_posix_memalign"]
    pub fn posix_memalign(memory: *mut *mut c_void, alignment: u32, byte_count: u32) -> i32;
    #[link_name = "eos_rust_free"]
    pub fn free(memory: *mut c_void);
    #[link_name = "eos_rust_abort"]
    pub fn abort() -> !;
    #[link_name = "eos_rust_exit"]
    pub fn exit(status: i32) -> !;
    #[link_name = "eos_rust_runtime_init"]
    pub fn eos_runtime_init(argc: i32, argv: *const *const c_char);

    #[link_name = "eos_rust_open"]
    pub fn open(path: *const c_char, flags: c_int, mode: mode_t) -> c_int;
    #[link_name = "eos_rust_close"]
    pub fn close(descriptor: c_int) -> c_int;
    #[link_name = "eos_rust_read"]
    pub fn read(descriptor: c_int, buffer: *mut c_void, byte_count: u32) -> i32;
    #[link_name = "eos_rust_write"]
    pub fn write(descriptor: c_int, buffer: *const c_void, byte_count: u32) -> i32;
    #[link_name = "eos_rust_pread"]
    pub fn pread(descriptor: c_int, buffer: *mut c_void, byte_count: u32, offset: i64) -> i32;
    #[link_name = "eos_rust_pwrite"]
    pub fn pwrite(descriptor: c_int, buffer: *const c_void, byte_count: u32, offset: i64) -> i32;
    #[link_name = "eos_rust_lseek"]
    pub fn lseek(descriptor: c_int, offset: i64, origin: c_int) -> i64;
    #[link_name = "eos_rust_fsync"]
    pub fn fsync(descriptor: c_int) -> c_int;
    #[link_name = "eos_rust_fstat"]
    pub fn fstat(descriptor: c_int, metadata: *mut stat) -> c_int;
    #[link_name = "eos_rust_stat_path"]
    pub fn stat(path: *const c_char, metadata: *mut stat) -> c_int;
    #[link_name = "eos_rust_lstat"]
    pub fn lstat(path: *const c_char, metadata: *mut stat) -> c_int;
    #[link_name = "eos_rust_mkdir"]
    pub fn mkdir(path: *const c_char, mode: mode_t) -> c_int;
    #[link_name = "eos_rust_unlink"]
    pub fn unlink(path: *const c_char) -> c_int;
    #[link_name = "eos_rust_rmdir"]
    pub fn rmdir(path: *const c_char) -> c_int;
    #[link_name = "eos_rust_rename"]
    pub fn rename(old_path: *const c_char, new_path: *const c_char) -> c_int;
    #[link_name = "eos_rust_realpath"]
    pub fn realpath(path: *const c_char, resolved: *mut c_char, capacity: u32) -> c_int;
    #[link_name = "eos_rust_getcwd"]
    pub fn getcwd(buffer: *mut c_char, capacity: u32) -> c_int;
    #[link_name = "eos_rust_chdir"]
    pub fn chdir(path: *const c_char) -> c_int;
    #[link_name = "eos_rust_opendir"]
    pub fn opendir(path: *const c_char) -> c_int;
    #[link_name = "eos_rust_readdir"]
    pub fn readdir(directory: c_int, entry: *mut dirent) -> c_int;
    #[link_name = "eos_rust_closedir"]
    pub fn closedir(directory: c_int) -> c_int;
    #[link_name = "eos_rust_fcntl"]
    pub fn fcntl(descriptor: c_int, command: c_int, argument: c_int) -> c_int;
    #[link_name = "eos_rust_dup"]
    pub fn dup(descriptor: c_int) -> c_int;
    #[link_name = "eos_rust_dup2"]
    pub fn dup2(old_descriptor: c_int, new_descriptor: c_int) -> c_int;
    #[link_name = "eos_rust_pipe"]
    pub fn pipe(descriptors: *mut c_int, flags: u32) -> c_int;
    #[link_name = "eos_rust_isatty"]
    pub fn isatty(descriptor: c_int) -> c_int;
    #[link_name = "eos_rust_gethostname"]
    pub fn gethostname(name: *mut c_char, capacity: u32) -> c_int;
    #[link_name = "eos_rust_symlink"]
    pub fn symlink(target: *const c_char, link_path: *const c_char) -> c_int;
    #[link_name = "eos_rust_link"]
    pub fn link(old_path: *const c_char, new_path: *const c_char) -> c_int;
    #[link_name = "eos_rust_chown"]
    pub fn chown(path: *const c_char, owner: u32, group: u32) -> c_int;
    #[link_name = "eos_rust_lchown"]
    pub fn lchown(path: *const c_char, owner: u32, group: u32) -> c_int;
    #[link_name = "eos_rust_fchown"]
    pub fn fchown(descriptor: c_int, owner: u32, group: u32) -> c_int;
    #[link_name = "eos_rust_chmod"]
    pub fn chmod(path: *const c_char, mode: mode_t) -> c_int;
    #[link_name = "eos_rust_fchmod"]
    pub fn fchmod(descriptor: c_int, mode: mode_t) -> c_int;

    #[link_name = "eos_rust_spawn"]
    pub fn eos_spawn(request: *const eos_rust_spawn_request, process: *mut eos_rust_process_t) -> i32;
    #[link_name = "eos_rust_process_wait"]
    pub fn eos_process_wait(process: eos_rust_process_t, status: *mut eos_rust_process_status) -> i32;
    #[link_name = "eos_rust_process_try_wait"]
    pub fn eos_process_try_wait(process: eos_rust_process_t, status: *mut eos_rust_process_status) -> i32;
    #[link_name = "eos_rust_process_kill"]
    pub fn eos_process_kill(process: eos_rust_process_t) -> i32;
    #[link_name = "eos_rust_process_close"]
    pub fn eos_process_close(process: eos_rust_process_t) -> i32;

    #[link_name = "eos_rust_pthread_create"]
    pub fn pthread_create(thread: *mut pthread_t, attr: *const pthread_attr_t, start: extern "C" fn(*mut c_void) -> *mut c_void, arg: *mut c_void) -> c_int;
    #[link_name = "eos_rust_pthread_join"]
    pub fn pthread_join(thread: pthread_t, result: *mut *mut c_void) -> c_int;
    #[link_name = "eos_rust_pthread_detach"]
    pub fn pthread_detach(thread: pthread_t) -> c_int;
    #[link_name = "eos_rust_pthread_self"]
    pub fn pthread_self() -> pthread_t;
    #[link_name = "eos_rust_pthread_equal"]
    pub fn pthread_equal(left: pthread_t, right: pthread_t) -> c_int;
    #[link_name = "eos_rust_pthread_attr_init"]
    pub fn pthread_attr_init(attr: *mut pthread_attr_t) -> c_int;
    #[link_name = "eos_rust_pthread_attr_destroy"]
    pub fn pthread_attr_destroy(attr: *mut pthread_attr_t) -> c_int;
    #[link_name = "eos_rust_pthread_attr_getstacksize"]
    pub fn pthread_attr_getstacksize(attr: *const pthread_attr_t, stack_size: *mut u32) -> c_int;
    #[link_name = "eos_rust_pthread_attr_setstacksize"]
    pub fn pthread_attr_setstacksize(attr: *mut pthread_attr_t, stack_size: u32) -> c_int;
    #[link_name = "eos_rust_pthread_yield"]
    pub fn pthread_yield() -> c_int;
    #[link_name = "eos_rust_pthread_getname_np"]
    pub fn pthread_getname_np(thread: pthread_t, name: *mut c_char, capacity: u32) -> c_int;
    #[link_name = "eos_rust_pthread_setname_np"]
    pub fn pthread_setname_np(thread: pthread_t, name: *const c_char) -> c_int;
    #[link_name = "eos_rust_pthread_key_create"]
    pub fn pthread_key_create(key: *mut pthread_key_t, destructor: Option<extern "C" fn(*mut c_void)>) -> c_int;
    #[link_name = "eos_rust_pthread_key_delete"]
    pub fn pthread_key_delete(key: pthread_key_t) -> c_int;
    #[link_name = "eos_rust_pthread_getspecific"]
    pub fn pthread_getspecific(key: pthread_key_t) -> *mut c_void;
    #[link_name = "eos_rust_pthread_setspecific"]
    pub fn pthread_setspecific(key: pthread_key_t, value: *const c_void) -> c_int;
    #[link_name = "eos_rust_pthread_mutexattr_init"]
    pub fn pthread_mutexattr_init(attr: *mut pthread_mutexattr_t) -> c_int;
    #[link_name = "eos_rust_pthread_mutexattr_destroy"]
    pub fn pthread_mutexattr_destroy(attr: *mut pthread_mutexattr_t) -> c_int;
    #[link_name = "eos_rust_pthread_mutexattr_settype"]
    pub fn pthread_mutexattr_settype(attr: *mut pthread_mutexattr_t, kind: c_int) -> c_int;
    #[link_name = "eos_rust_pthread_mutex_init"]
    pub fn pthread_mutex_init(mutex: *mut pthread_mutex_t, attr: *const pthread_mutexattr_t) -> c_int;
    #[link_name = "eos_rust_pthread_mutex_destroy"]
    pub fn pthread_mutex_destroy(mutex: *mut pthread_mutex_t) -> c_int;
    #[link_name = "eos_rust_pthread_mutex_lock"]
    pub fn pthread_mutex_lock(mutex: *mut pthread_mutex_t) -> c_int;
    #[link_name = "eos_rust_pthread_mutex_trylock"]
    pub fn pthread_mutex_trylock(mutex: *mut pthread_mutex_t) -> c_int;
    #[link_name = "eos_rust_pthread_mutex_unlock"]
    pub fn pthread_mutex_unlock(mutex: *mut pthread_mutex_t) -> c_int;
    #[link_name = "eos_rust_pthread_condattr_init"]
    pub fn pthread_condattr_init(attr: *mut pthread_condattr_t) -> c_int;
    #[link_name = "eos_rust_pthread_condattr_destroy"]
    pub fn pthread_condattr_destroy(attr: *mut pthread_condattr_t) -> c_int;
    #[link_name = "eos_rust_pthread_condattr_setclock"]
    pub fn pthread_condattr_setclock(attr: *mut pthread_condattr_t, clock_id: c_int) -> c_int;
    #[link_name = "eos_rust_pthread_cond_init"]
    pub fn pthread_cond_init(condition: *mut pthread_cond_t, attr: *const pthread_condattr_t) -> c_int;
    #[link_name = "eos_rust_pthread_cond_destroy"]
    pub fn pthread_cond_destroy(condition: *mut pthread_cond_t) -> c_int;
    #[link_name = "eos_rust_pthread_cond_signal"]
    pub fn pthread_cond_signal(condition: *mut pthread_cond_t) -> c_int;
    #[link_name = "eos_rust_pthread_cond_broadcast"]
    pub fn pthread_cond_broadcast(condition: *mut pthread_cond_t) -> c_int;
    #[link_name = "eos_rust_pthread_cond_wait"]
    pub fn pthread_cond_wait(condition: *mut pthread_cond_t, mutex: *mut pthread_mutex_t) -> c_int;
    #[link_name = "eos_rust_pthread_cond_timedwait"]
    pub fn pthread_cond_timedwait(condition: *mut pthread_cond_t, mutex: *mut pthread_mutex_t, deadline: *const timespec) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_init"]
    pub fn pthread_rwlock_init(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_destroy"]
    pub fn pthread_rwlock_destroy(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_rdlock"]
    pub fn pthread_rwlock_rdlock(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_tryrdlock"]
    pub fn pthread_rwlock_tryrdlock(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_wrlock"]
    pub fn pthread_rwlock_wrlock(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_trywrlock"]
    pub fn pthread_rwlock_trywrlock(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_rwlock_unlock"]
    pub fn pthread_rwlock_unlock(rwlock: *mut pthread_rwlock_t) -> c_int;
    #[link_name = "eos_rust_pthread_once"]
    pub fn pthread_once(control: *mut pthread_once_t, initialization: extern "C" fn()) -> c_int;
    #[link_name = "eos_rust_clock_gettime"]
    pub fn clock_gettime(clock_id: clockid_t, time: *mut timespec) -> c_int;
    #[link_name = "eos_rust_nanosleep"]
    pub fn nanosleep(requested: *const timespec, remaining: *mut timespec) -> c_int;

    #[link_name = "eos_rust_socket"]
    pub fn socket(domain: c_int, kind: c_int, protocol: c_int) -> c_int;
    #[link_name = "eos_rust_bind"]
    pub fn bind(descriptor: c_int, address: *const sockaddr, address_length: socklen_t) -> c_int;
    #[link_name = "eos_rust_connect"]
    pub fn connect(descriptor: c_int, address: *const sockaddr, address_length: socklen_t) -> c_int;
    #[link_name = "eos_rust_listen"]
    pub fn listen(descriptor: c_int, backlog: c_int) -> c_int;
    #[link_name = "eos_rust_accept"]
    pub fn accept(descriptor: c_int, address: *mut sockaddr, address_length: *mut socklen_t) -> c_int;
    #[link_name = "eos_rust_send"]
    pub fn send(descriptor: c_int, buffer: *const c_void, byte_count: u32, flags: c_int) -> i32;
    #[link_name = "eos_rust_recv"]
    pub fn recv(descriptor: c_int, buffer: *mut c_void, byte_count: u32, flags: c_int) -> i32;
    #[link_name = "eos_rust_sendto"]
    pub fn sendto(descriptor: c_int, buffer: *const c_void, byte_count: u32, flags: c_int, destination: *const sockaddr, destination_length: socklen_t) -> i32;
    #[link_name = "eos_rust_recvfrom"]
    pub fn recvfrom(descriptor: c_int, buffer: *mut c_void, byte_count: u32, flags: c_int, source: *mut sockaddr, source_length: *mut socklen_t) -> i32;
    #[link_name = "eos_rust_shutdown"]
    pub fn shutdown(descriptor: c_int, how: c_int) -> c_int;
    #[link_name = "eos_rust_getsockname"]
    pub fn getsockname(descriptor: c_int, address: *mut sockaddr, address_length: *mut socklen_t) -> c_int;
    #[link_name = "eos_rust_getpeername"]
    pub fn getpeername(descriptor: c_int, address: *mut sockaddr, address_length: *mut socklen_t) -> c_int;
    #[link_name = "eos_rust_setsockopt"]
    pub fn setsockopt(descriptor: c_int, level: c_int, option_name: c_int, option_value: *const c_void, option_length: socklen_t) -> c_int;
    #[link_name = "eos_rust_getsockopt"]
    pub fn getsockopt(descriptor: c_int, level: c_int, option_name: c_int, option_value: *mut c_void, option_length: *mut socklen_t) -> c_int;
    #[link_name = "eos_rust_poll"]
    pub fn poll(descriptors: *mut pollfd, descriptor_count: nfds_t, timeout_milliseconds: c_int) -> c_int;
    #[link_name = "eos_rust_inet_pton"]
    pub fn inet_pton(family: c_int, source: *const c_char, destination: *mut c_void) -> c_int;
    #[link_name = "eos_rust_inet_ntop"]
    pub fn inet_ntop(family: c_int, source: *const c_void, destination: *mut c_char, capacity: socklen_t) -> *const c_char;
    #[link_name = "eos_rust_getaddrinfo"]
    pub fn getaddrinfo(node: *const c_char, service: *const c_char, hints: *const addrinfo, result: *mut *mut addrinfo) -> c_int;
    #[link_name = "eos_rust_freeaddrinfo"]
    pub fn freeaddrinfo(result: *mut addrinfo);
    #[link_name = "eos_rust_gai_strerror"]
    pub fn gai_strerror(error_code: c_int) -> *const c_char;

    #[link_name = "eos_rust_getenv"]
    pub fn getenv(name: *const c_char) -> *mut c_char;
    #[link_name = "eos_rust_setenv"]
    pub fn setenv(name: *const c_char, value: *const c_char, overwrite: c_int) -> c_int;
    #[link_name = "eos_rust_unsetenv"]
    pub fn unsetenv(name: *const c_char) -> c_int;
    #[link_name = "eos_rust_environ"]
    pub fn environ() -> *mut *mut c_char;
    #[link_name = "eos_rust_hash_seed"]
    pub fn eos_hash_seed(key0: *mut u64, key1: *mut u64);
}
