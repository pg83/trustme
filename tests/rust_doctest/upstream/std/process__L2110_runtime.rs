// Extracted from library/std/src/process.rs:2110
#![allow(unused)]
#![feature(exitcode_exit_method)]
fn main() {
    use std::process::ExitCode;
    use std::fmt;
    enum UhOhError { GenericProblem, Specific, WithCode { exit_code: ExitCode, _x: () } }
    impl fmt::Display for UhOhError {
        fn fmt(&self, _: &mut fmt::Formatter<'_>) -> fmt::Result { unimplemented!() }
    }
    // there's no way to gracefully recover from an UhOhError, so we just
    // print a message and exit
    fn handle_unrecoverable_error(err: UhOhError) -> ! {
        eprintln!("UH OH! {err}");
        let code = match err {
            UhOhError::GenericProblem => ExitCode::FAILURE,
            UhOhError::Specific => ExitCode::from(3),
            UhOhError::WithCode { exit_code, .. } => exit_code,
        };
        code.exit_process()
    }
}
