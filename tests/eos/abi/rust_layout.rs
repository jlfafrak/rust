#![no_std]

extern crate libc;

use core::mem::{align_of, offset_of, size_of};

macro_rules! emit {
    ($name:ident, $value:expr) => {
        #[used]
        #[unsafe(no_mangle)]
        pub static $name: [u8; $value + 1] = [0; $value + 1];
    };
}

macro_rules! layout {
    ($size:ident, $align:ident, $ty:ty) => {
        emit!($size, size_of::<$ty>());
        emit!($align, align_of::<$ty>());
    };
}

layout!(eos_layout_sizeof_timespec, eos_layout_alignof_timespec, libc::timespec);
emit!(eos_layout_offsetof_timespec_tv_sec, offset_of!(libc::timespec, tv_sec));
emit!(eos_layout_offsetof_timespec_tv_nsec, offset_of!(libc::timespec, tv_nsec));
layout!(eos_layout_sizeof_timeval, eos_layout_alignof_timeval, libc::timeval);
emit!(eos_layout_offsetof_timeval_tv_sec, offset_of!(libc::timeval, tv_sec));
emit!(eos_layout_offsetof_timeval_tv_usec, offset_of!(libc::timeval, tv_usec));

layout!(eos_layout_sizeof_stat, eos_layout_alignof_stat, libc::stat);
emit!(eos_layout_offsetof_stat_st_dev, offset_of!(libc::stat, st_dev));
emit!(eos_layout_offsetof_stat_st_ino, offset_of!(libc::stat, st_ino));
emit!(eos_layout_offsetof_stat_st_mode, offset_of!(libc::stat, st_mode));
emit!(eos_layout_offsetof_stat_st_nlink, offset_of!(libc::stat, st_nlink));
emit!(eos_layout_offsetof_stat_st_uid, offset_of!(libc::stat, st_uid));
emit!(eos_layout_offsetof_stat_st_gid, offset_of!(libc::stat, st_gid));
emit!(eos_layout_offsetof_stat_st_size, offset_of!(libc::stat, st_size));
emit!(eos_layout_offsetof_stat_st_atime, offset_of!(libc::stat, st_atime));
emit!(eos_layout_offsetof_stat_st_atime_nsec, offset_of!(libc::stat, st_atime_nsec));
emit!(eos_layout_offsetof_stat_st_mtime, offset_of!(libc::stat, st_mtime));
emit!(eos_layout_offsetof_stat_st_mtime_nsec, offset_of!(libc::stat, st_mtime_nsec));
emit!(eos_layout_offsetof_stat_st_ctime, offset_of!(libc::stat, st_ctime));
emit!(eos_layout_offsetof_stat_st_ctime_nsec, offset_of!(libc::stat, st_ctime_nsec));
emit!(eos_layout_offsetof_stat_st_blocks, offset_of!(libc::stat, st_blocks));
emit!(eos_layout_offsetof_stat_st_blksize, offset_of!(libc::stat, st_blksize));
emit!(eos_layout_offsetof_stat_reserved, offset_of!(libc::stat, reserved));

layout!(eos_layout_sizeof_dirent, eos_layout_alignof_dirent, libc::dirent);
emit!(eos_layout_offsetof_dirent_d_ino, offset_of!(libc::dirent, d_ino));
emit!(eos_layout_offsetof_dirent_d_type, offset_of!(libc::dirent, d_type));
emit!(eos_layout_offsetof_dirent_d_name_length, offset_of!(libc::dirent, d_name_length));
emit!(eos_layout_offsetof_dirent_d_name, offset_of!(libc::dirent, d_name));
layout!(eos_layout_sizeof_iovec, eos_layout_alignof_iovec, libc::iovec);
emit!(eos_layout_offsetof_iovec_iov_base, offset_of!(libc::iovec, iov_base));
emit!(eos_layout_offsetof_iovec_iov_len, offset_of!(libc::iovec, iov_len));

layout!(eos_layout_sizeof_pthread_attr_t, eos_layout_alignof_pthread_attr_t, libc::pthread_attr_t);
layout!(eos_layout_sizeof_pthread_mutex_t, eos_layout_alignof_pthread_mutex_t, libc::pthread_mutex_t);
layout!(eos_layout_sizeof_pthread_mutexattr_t, eos_layout_alignof_pthread_mutexattr_t, libc::pthread_mutexattr_t);
layout!(eos_layout_sizeof_pthread_cond_t, eos_layout_alignof_pthread_cond_t, libc::pthread_cond_t);
layout!(eos_layout_sizeof_pthread_condattr_t, eos_layout_alignof_pthread_condattr_t, libc::pthread_condattr_t);
layout!(eos_layout_sizeof_pthread_rwlock_t, eos_layout_alignof_pthread_rwlock_t, libc::pthread_rwlock_t);
layout!(eos_layout_sizeof_pthread_once_t, eos_layout_alignof_pthread_once_t, libc::pthread_once_t);

layout!(eos_layout_sizeof_in_addr, eos_layout_alignof_in_addr, libc::in_addr);
emit!(eos_layout_offsetof_in_addr_s_addr, offset_of!(libc::in_addr, s_addr));
layout!(eos_layout_sizeof_in6_addr, eos_layout_alignof_in6_addr, libc::in6_addr);
emit!(eos_layout_offsetof_in6_addr_s6_addr, offset_of!(libc::in6_addr, s6_addr));
layout!(eos_layout_sizeof_sockaddr, eos_layout_alignof_sockaddr, libc::sockaddr);
emit!(eos_layout_offsetof_sockaddr_sa_family, offset_of!(libc::sockaddr, sa_family));
emit!(eos_layout_offsetof_sockaddr_sa_data, offset_of!(libc::sockaddr, sa_data));
layout!(eos_layout_sizeof_sockaddr_in, eos_layout_alignof_sockaddr_in, libc::sockaddr_in);
emit!(eos_layout_offsetof_sockaddr_in_sin_family, offset_of!(libc::sockaddr_in, sin_family));
emit!(eos_layout_offsetof_sockaddr_in_sin_port, offset_of!(libc::sockaddr_in, sin_port));
emit!(eos_layout_offsetof_sockaddr_in_sin_addr, offset_of!(libc::sockaddr_in, sin_addr));
emit!(eos_layout_offsetof_sockaddr_in_sin_zero, offset_of!(libc::sockaddr_in, sin_zero));
layout!(eos_layout_sizeof_sockaddr_in6, eos_layout_alignof_sockaddr_in6, libc::sockaddr_in6);
emit!(eos_layout_offsetof_sockaddr_in6_sin6_family, offset_of!(libc::sockaddr_in6, sin6_family));
emit!(eos_layout_offsetof_sockaddr_in6_sin6_port, offset_of!(libc::sockaddr_in6, sin6_port));
emit!(eos_layout_offsetof_sockaddr_in6_sin6_flowinfo, offset_of!(libc::sockaddr_in6, sin6_flowinfo));
emit!(eos_layout_offsetof_sockaddr_in6_sin6_addr, offset_of!(libc::sockaddr_in6, sin6_addr));
emit!(eos_layout_offsetof_sockaddr_in6_sin6_scope_id, offset_of!(libc::sockaddr_in6, sin6_scope_id));
layout!(eos_layout_sizeof_sockaddr_storage, eos_layout_alignof_sockaddr_storage, libc::sockaddr_storage);
emit!(eos_layout_offsetof_sockaddr_storage_ss_family, offset_of!(libc::sockaddr_storage, ss_family));
emit!(eos_layout_offsetof_sockaddr_storage_ss_data, offset_of!(libc::sockaddr_storage, ss_data));
emit!(eos_layout_offsetof_sockaddr_storage_ss_align, offset_of!(libc::sockaddr_storage, ss_align));

layout!(eos_layout_sizeof_pollfd, eos_layout_alignof_pollfd, libc::pollfd);
emit!(eos_layout_offsetof_pollfd_fd, offset_of!(libc::pollfd, fd));
emit!(eos_layout_offsetof_pollfd_events, offset_of!(libc::pollfd, events));
emit!(eos_layout_offsetof_pollfd_revents, offset_of!(libc::pollfd, revents));
layout!(eos_layout_sizeof_addrinfo, eos_layout_alignof_addrinfo, libc::addrinfo);
emit!(eos_layout_offsetof_addrinfo_ai_flags, offset_of!(libc::addrinfo, ai_flags));
emit!(eos_layout_offsetof_addrinfo_ai_family, offset_of!(libc::addrinfo, ai_family));
emit!(eos_layout_offsetof_addrinfo_ai_socktype, offset_of!(libc::addrinfo, ai_socktype));
emit!(eos_layout_offsetof_addrinfo_ai_protocol, offset_of!(libc::addrinfo, ai_protocol));
emit!(eos_layout_offsetof_addrinfo_ai_addrlen, offset_of!(libc::addrinfo, ai_addrlen));
emit!(eos_layout_offsetof_addrinfo_ai_addr, offset_of!(libc::addrinfo, ai_addr));
emit!(eos_layout_offsetof_addrinfo_ai_canonname, offset_of!(libc::addrinfo, ai_canonname));
emit!(eos_layout_offsetof_addrinfo_ai_next, offset_of!(libc::addrinfo, ai_next));

layout!(eos_layout_sizeof_spawn_request, eos_layout_alignof_spawn_request, libc::eos_rust_spawn_request);
emit!(eos_layout_offsetof_spawn_request_program, offset_of!(libc::eos_rust_spawn_request, program));
emit!(eos_layout_offsetof_spawn_request_argv, offset_of!(libc::eos_rust_spawn_request, argv));
emit!(eos_layout_offsetof_spawn_request_argc, offset_of!(libc::eos_rust_spawn_request, argc));
emit!(eos_layout_offsetof_spawn_request_envp, offset_of!(libc::eos_rust_spawn_request, envp));
emit!(eos_layout_offsetof_spawn_request_envc, offset_of!(libc::eos_rust_spawn_request, envc));
emit!(eos_layout_offsetof_spawn_request_cwd, offset_of!(libc::eos_rust_spawn_request, cwd));
emit!(eos_layout_offsetof_spawn_request_stdin_fd, offset_of!(libc::eos_rust_spawn_request, stdin_fd));
emit!(eos_layout_offsetof_spawn_request_stdout_fd, offset_of!(libc::eos_rust_spawn_request, stdout_fd));
emit!(eos_layout_offsetof_spawn_request_stderr_fd, offset_of!(libc::eos_rust_spawn_request, stderr_fd));
emit!(eos_layout_offsetof_spawn_request_flags, offset_of!(libc::eos_rust_spawn_request, flags));
emit!(eos_layout_offsetof_spawn_request_reserved, offset_of!(libc::eos_rust_spawn_request, reserved));
layout!(eos_layout_sizeof_process_status, eos_layout_alignof_process_status, libc::eos_rust_process_status);
emit!(eos_layout_offsetof_process_status_kind, offset_of!(libc::eos_rust_process_status, kind));
emit!(eos_layout_offsetof_process_status_code, offset_of!(libc::eos_rust_process_status, code));
emit!(eos_layout_offsetof_process_status_reserved, offset_of!(libc::eos_rust_process_status, reserved));
