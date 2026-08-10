// Extracted from library/std/src/process.rs:2054
use std::process::ExitCode;
fn check_foo() -> bool { true }

fn main() -> ExitCode {
    if !check_foo() {
        return ExitCode::from(42);
    }

    ExitCode::SUCCESS
}
