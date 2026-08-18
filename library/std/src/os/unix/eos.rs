//! Internal EOS definitions needed by the portable Unix extension traits.

pub mod fs {
    use crate::fs::Metadata;
    use crate::sys::AsInner;

    pub trait MetadataExt {
        fn st_dev(&self) -> u64;
        fn st_ino(&self) -> u64;
        fn st_mode(&self) -> u32;
        fn st_nlink(&self) -> u64;
        fn st_uid(&self) -> u32;
        fn st_gid(&self) -> u32;
        fn st_rdev(&self) -> u64;
        fn st_size(&self) -> u64;
        fn st_atime(&self) -> i64;
        fn st_atime_nsec(&self) -> i64;
        fn st_mtime(&self) -> i64;
        fn st_mtime_nsec(&self) -> i64;
        fn st_ctime(&self) -> i64;
        fn st_ctime_nsec(&self) -> i64;
        fn st_blksize(&self) -> u64;
        fn st_blocks(&self) -> u64;
    }

    impl MetadataExt for Metadata {
        fn st_dev(&self) -> u64 {
            self.as_inner().as_inner().st_dev
        }
        fn st_ino(&self) -> u64 {
            self.as_inner().as_inner().st_ino
        }
        fn st_mode(&self) -> u32 {
            self.as_inner().as_inner().st_mode
        }
        fn st_nlink(&self) -> u64 {
            self.as_inner().as_inner().st_nlink.into()
        }
        fn st_uid(&self) -> u32 {
            self.as_inner().as_inner().st_uid
        }
        fn st_gid(&self) -> u32 {
            self.as_inner().as_inner().st_gid
        }
        fn st_rdev(&self) -> u64 {
            // EOS v1 stat metadata has no special-device identifier.
            0
        }
        fn st_size(&self) -> u64 {
            self.as_inner().as_inner().st_size
        }
        fn st_atime(&self) -> i64 {
            self.as_inner().as_inner().st_atime
        }
        fn st_atime_nsec(&self) -> i64 {
            self.as_inner().as_inner().st_atime_nsec
        }
        fn st_mtime(&self) -> i64 {
            self.as_inner().as_inner().st_mtime
        }
        fn st_mtime_nsec(&self) -> i64 {
            self.as_inner().as_inner().st_mtime_nsec
        }
        fn st_ctime(&self) -> i64 {
            self.as_inner().as_inner().st_ctime
        }
        fn st_ctime_nsec(&self) -> i64 {
            self.as_inner().as_inner().st_ctime_nsec
        }
        fn st_blksize(&self) -> u64 {
            self.as_inner().as_inner().st_blksize.into()
        }
        fn st_blocks(&self) -> u64 {
            self.as_inner().as_inner().st_blocks
        }
    }
}

#[allow(deprecated)]
pub mod raw {
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type blkcnt_t = libc::blkcnt_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type blksize_t = libc::blksize_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type dev_t = libc::dev_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type ino_t = libc::ino_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type mode_t = libc::mode_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type nlink_t = libc::nlink_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type off_t = libc::off_t;
    #[stable(feature = "pthread_t", since = "1.8.0")]
    pub type pthread_t = libc::pthread_t;
    #[stable(feature = "raw_ext", since = "1.1.0")]
    pub type time_t = libc::time_t;
}
