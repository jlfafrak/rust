use libc::c_int;

use super::common::*;
use crate::fmt;
use crate::io;
use crate::num::NonZero;
use crate::process::StdioPipes;

impl Command {
    pub fn spawn(
        &mut self,
        default: Stdio,
        needs_stdin: bool,
    ) -> io::Result<(Process, StdioPipes)> {
        let envp = self.capture_env();
        if self.saw_nul() {
            return Err(io::const_error!(
                io::ErrorKind::InvalidInput,
                "nul byte found in provided data",
            ));
        }
        if self.get_uid().is_some()
            || self.get_gid().is_some()
            || self.get_groups().is_some()
            || self.get_pgroup().is_some()
            || self.get_chroot().is_some()
            || self.get_setsid()
            || !self.get_closures().is_empty()
        {
            return Err(io::const_error!(
                io::ErrorKind::Unsupported,
                "the requested process attributes are not supported on EOS",
            ));
        }

        let argc = u32::try_from(self.get_argv().iter().len()).map_err(|_| {
            io::const_error!(io::ErrorKind::InvalidInput, "too many process arguments")
        })?;
        let envc = match &envp {
            Some(envp) => u32::try_from(envp.iter().len()).map_err(|_| {
                io::const_error!(io::ErrorKind::InvalidInput, "too many environment entries")
            })?,
            None => 0,
        };
        let (ours, theirs) = self.setup_io(default, needs_stdin)?;
        let request = libc::eos_rust_spawn_request {
            program: self.get_program_cstr().as_ptr(),
            argv: self.get_argv().as_ptr(),
            argc,
            envp: envp.as_ref().map_or(core::ptr::null(), |env| env.as_ptr()),
            envc,
            cwd: self.get_cwd().map_or(core::ptr::null(), |cwd| cwd.as_ptr()),
            stdin_fd: theirs.stdin.fd().unwrap_or(-1),
            stdout_fd: theirs.stdout.fd().unwrap_or(-1),
            stderr_fd: theirs.stderr.fd().unwrap_or(-1),
            flags: 0,
            reserved: [0; 7],
        };
        let mut handle = 0;
        // EOS copies the request through the stable eos_rust_spawn service.
        if unsafe { libc::eos_spawn(&request, &mut handle) } != 0 {
            return Err(io::Error::last_os_error());
        }
        Ok((Process { handle, status: None }, ours))
    }

    pub fn exec(&mut self, _default: Stdio) -> io::Error {
        io::Error::new(
            io::ErrorKind::Unsupported,
            "replacing the current EOS application is not supported",
        )
    }
}

pub struct Process {
    handle: libc::eos_rust_process_t,
    status: Option<ExitStatus>,
}

impl Process {
    pub fn id(&self) -> u32 {
        self.handle
    }

    pub fn kill(&self) -> io::Result<()> {
        if self.status.is_some() {
            return Ok(());
        }
        if unsafe { libc::eos_process_kill(self.handle) } == 0 {
            Ok(())
        } else {
            Err(io::Error::last_os_error())
        }
    }

    pub(crate) fn send_signal(&self, _signal: i32) -> io::Result<()> {
        Err(io::const_error!(
            io::ErrorKind::Unsupported,
            "EOS v1 does not expose process signals",
        ))
    }

    pub(crate) fn send_process_group_signal(&self, _signal: i32) -> io::Result<()> {
        Err(io::const_error!(
            io::ErrorKind::Unsupported,
            "EOS v1 does not expose process groups",
        ))
    }

    pub fn wait(&mut self) -> io::Result<ExitStatus> {
        if let Some(status) = self.status {
            return Ok(status);
        }
        let mut status = libc::eos_rust_process_status {
            kind: 0,
            code: 0,
            reserved: [0; 6],
        };
        if unsafe { libc::eos_process_wait(self.handle, &mut status) } != 0 {
            return Err(io::Error::last_os_error());
        }
        let status = ExitStatus::from_eos(status)?;
        self.status = Some(status);
        Ok(status)
    }

    pub fn try_wait(&mut self) -> io::Result<Option<ExitStatus>> {
        if let Some(status) = self.status {
            return Ok(Some(status));
        }
        let mut status = libc::eos_rust_process_status {
            kind: 0,
            code: 0,
            reserved: [0; 6],
        };
        match unsafe { libc::eos_process_try_wait(self.handle, &mut status) } {
            0 => Ok(None),
            1 => {
                let status = ExitStatus::from_eos(status)?;
                self.status = Some(status);
                Ok(Some(status))
            }
            _ => Err(io::Error::last_os_error()),
        }
    }
}

impl Drop for Process {
    fn drop(&mut self) {
        if unsafe { libc::eos_process_close(self.handle) } != 0 {
            unsafe { libc::abort() }
        }
    }
}

#[derive(PartialEq, Eq, Clone, Copy, Default)]
pub struct ExitStatus {
    kind: u32,
    code: i32,
}

impl ExitStatus {
    fn from_eos(status: libc::eos_rust_process_status) -> io::Result<Self> {
        match status.kind {
            libc::EOS_RUST_PROCESS_EXITED | libc::EOS_RUST_PROCESS_TERMINATED => {
                Ok(Self { kind: status.kind, code: status.code })
            }
            _ => Err(io::const_error!(
                io::ErrorKind::InvalidData,
                "EOS returned an invalid process status",
            )),
        }
    }

    pub fn exit_ok(&self) -> Result<(), ExitStatusError> {
        if self.kind == libc::EOS_RUST_PROCESS_EXITED && self.code == 0 {
            Ok(())
        } else {
            Err(ExitStatusError(*self))
        }
    }

    pub fn code(&self) -> Option<i32> {
        (self.kind == libc::EOS_RUST_PROCESS_EXITED).then_some(self.code)
    }

    pub fn signal(&self) -> Option<i32> {
        None
    }

    pub fn core_dumped(&self) -> bool {
        false
    }

    pub fn stopped_signal(&self) -> Option<i32> {
        None
    }

    pub fn continued(&self) -> bool {
        false
    }

    pub fn into_raw(&self) -> c_int {
        if self.kind == libc::EOS_RUST_PROCESS_EXITED {
            (self.code & 0xff) << 8
        } else {
            self.code
        }
    }
}

impl From<c_int> for ExitStatus {
    fn from(raw: c_int) -> Self {
        Self { kind: libc::EOS_RUST_PROCESS_EXITED, code: (raw >> 8) & 0xff }
    }
}

impl fmt::Debug for ExitStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("eos_process_status")
            .field("kind", &self.kind)
            .field("code", &self.code)
            .finish()
    }
}

impl fmt::Display for ExitStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.kind == libc::EOS_RUST_PROCESS_EXITED {
            write!(f, "exit status: {}", self.code)
        } else {
            write!(f, "terminated with EOS status: {}", self.code)
        }
    }
}

#[derive(PartialEq, Eq, Clone, Copy)]
pub struct ExitStatusError(ExitStatus);

impl Into<ExitStatus> for ExitStatusError {
    fn into(self) -> ExitStatus {
        self.0
    }
}

impl fmt::Debug for ExitStatusError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.0.fmt(f)
    }
}

impl ExitStatusError {
    pub fn code(self) -> Option<NonZero<i32>> {
        self.0.code().and_then(NonZero::new)
    }
}
