use std::any::Any;
#[cfg(all(unix, not(target_os = "eos")))]
use std::os::unix::process::ExitStatusExt;
use std::process::ExitStatus;

pub use self::TestResult::*;
use super::bench::BenchSamples;
use super::options::ShouldPanic;
use super::time;
use super::types::TestDesc;

// Return code for secondary process.
// Start somewhere other than 0 so we know the return code means what we think
// it means.
pub(crate) const TR_OK: i32 = 50;

// On Windows we use __fastfail to abort, which is documented to use this
// exception code.
#[cfg(windows)]
const STATUS_FAIL_FAST_EXCEPTION: i32 = 0xC0000409u32 as i32;

// On Zircon (the Fuchsia kernel), an abort from userspace calls the
// LLVM implementation of __builtin_trap(), e.g., ud2 on x86, which
// raises a kernel exception. If a userspace process does not
// otherwise arrange exception handling, the kernel kills the process
// with this return code.
#[cfg(target_os = "fuchsia")]
const ZX_TASK_RETCODE_EXCEPTION_KILL: i32 = -1028;

#[derive(Debug, Clone, PartialEq)]
pub enum TestResult {
    TrOk,
    TrFailed,
    TrFailedMsg(String),
    TrIgnored,
    TrBench(BenchSamples),
    TrTimedFail,
}

/// Creates a `TestResult` depending on the raw result of test execution
/// and associated data.
pub(crate) fn calc_result(
    desc: &TestDesc,
    panic_payload: Option<&(dyn Any + Send)>,
    time_opts: Option<&time::TestTimeOptions>,
    exec_time: Option<&time::TestExecTime>,
) -> TestResult {
    let result = match (desc.should_panic, panic_payload) {
        // The test did or didn't panic, as expected.
        (ShouldPanic::No, None) | (ShouldPanic::Yes, Some(_)) => TestResult::TrOk,

        // Check the actual panic message against the expected message.
        (ShouldPanic::YesWithMessage(msg), Some(err)) => {
            let maybe_panic_str = err
                .downcast_ref::<String>()
                .map(|e| &**e)
                .or_else(|| err.downcast_ref::<&'static str>().copied());

            if maybe_panic_str.map(|e| e.contains(msg)).unwrap_or(false) {
                TestResult::TrOk
            } else if let Some(panic_str) = maybe_panic_str {
                TestResult::TrFailedMsg(format!(
                    r#"panic did not contain expected string
      panic message: {panic_str:?}
 expected substring: {msg:?}"#
                ))
            } else {
                TestResult::TrFailedMsg(format!(
                    r#"expected panic with string value,
 found non-string value: `{:?}`
     expected substring: {msg:?}"#,
                    (*err).type_id()
                ))
            }
        }

        // The test should have panicked, but didn't panic.
        (ShouldPanic::Yes, None) | (ShouldPanic::YesWithMessage(_), None) => {
            let fn_location = if !desc.source_file.is_empty() {
                &format!(" at {}:{}:{}", desc.source_file, desc.start_line, desc.start_col)
            } else {
                ""
            };
            TestResult::TrFailedMsg(format!("test did not panic as expected{}", fn_location))
        }

        // The test should not have panicked, but did panic.
        (ShouldPanic::No, Some(_)) => TestResult::TrFailed,
    };

    // If test is already failed (or allowed to fail), do not change the result.
    if result != TestResult::TrOk {
        return result;
    }

    // Check if test is failed due to timeout.
    if let (Some(opts), Some(time)) = (time_opts, exec_time) {
        if opts.error_on_excess && opts.is_critical(desc, time) {
            return TestResult::TrTimedFail;
        }
    }

    result
}

/// Creates a `TestResult` depending on the exit code of test subprocess.
pub(crate) fn get_result_from_exit_code(
    desc: &TestDesc,
    status: ExitStatus,
    time_opts: Option<&time::TestTimeOptions>,
    exec_time: Option<&time::TestExecTime>,
) -> TestResult {
    #[cfg(target_os = "eos")]
    let result = eos_result_from_status(status.code(), &status);
    #[cfg(not(target_os = "eos"))]
    let result = match status.code() {
        Some(TR_OK) => TestResult::TrOk,
        #[cfg(windows)]
        Some(STATUS_FAIL_FAST_EXCEPTION) => TestResult::TrFailed,
        #[cfg(unix)]
        None => match status.signal() {
            Some(libc::SIGABRT) => TestResult::TrFailed,
            Some(signal) => {
                TestResult::TrFailedMsg(format!("child process exited with signal {signal}"))
            }
            None => unreachable!("status.code() returned None but status.signal() was None"),
        },
        // Upon an abort, Fuchsia returns the status code ZX_TASK_RETCODE_EXCEPTION_KILL.
        #[cfg(target_os = "fuchsia")]
        Some(ZX_TASK_RETCODE_EXCEPTION_KILL) => TestResult::TrFailed,
        #[cfg(not(unix))]
        None => TestResult::TrFailedMsg(format!("unknown return code")),
        #[cfg(any(windows, unix))]
        Some(code) => TestResult::TrFailedMsg(format!("got unexpected return code {code}")),
        #[cfg(not(any(windows, unix)))]
        Some(_) => TestResult::TrFailed,
    };

    // If test is already failed (or allowed to fail), do not change the result.
    if result != TestResult::TrOk {
        return result;
    }

    // Check if test is failed due to timeout.
    if let (Some(opts), Some(time)) = (time_opts, exec_time) {
        if opts.error_on_excess && opts.is_critical(desc, time) {
            return TestResult::TrTimedFail;
        }
    }

    result
}

#[cfg(any(test, target_os = "eos"))]
fn eos_result_from_status(
    code: Option<i32>,
    status: &impl std::fmt::Display,
) -> TestResult {
    match code {
        Some(TR_OK) => TestResult::TrOk,
        Some(code) => TestResult::TrFailedMsg(format!("got unexpected return code {code}")),
        None => TestResult::TrFailedMsg(format!("child process {status}")),
    }
}

#[cfg(test)]
mod tests {
    use super::{TR_OK, TestResult, eos_result_from_status};
    use std::fmt;

    struct EosStatus {
        kind: &'static str,
        code: i32,
        reserved: [u32; 6],
    }

    impl fmt::Display for EosStatus {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            match self.kind {
                "exited" => write!(formatter, "exit status: {}", self.code),
                "terminated" => {
                    write!(formatter, "terminated with EOS status: {}", self.code)
                }
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn eos_exited_status_uses_the_full_exit_code() {
        let reserved = [1, 2, 3, 4, 5, 6];
        let ok = EosStatus { kind: "exited", code: TR_OK, reserved };
        let failed = EosStatus { kind: "exited", code: 257, reserved };

        assert_eq!(eos_result_from_status(Some(ok.code), &ok), TestResult::TrOk);
        assert_eq!(
            eos_result_from_status(Some(failed.code), &failed),
            TestResult::TrFailedMsg("got unexpected return code 257".into()),
        );
        assert_eq!(ok.reserved, reserved);
        assert_eq!(failed.reserved, reserved);
    }

    #[test]
    fn eos_terminated_status_is_truthful_without_inventing_a_signal() {
        let status = EosStatus {
            kind: "terminated",
            code: 0x1234,
            reserved: [7, 8, 9, 10, 11, 12],
        };

        let result = eos_result_from_status(None, &status);

        assert_eq!(
            result,
            TestResult::TrFailedMsg(
                "child process terminated with EOS status: 4660".into(),
            ),
        );
        assert!(!format!("{result:?}").contains("signal"));
        assert_eq!(status.reserved, [7, 8, 9, 10, 11, 12]);
    }
}
