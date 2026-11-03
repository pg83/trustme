//@ compile-flags: -Zmir-opt-level=0 -Coverflow-checks=yes -Aoverflowing_literals
#![allow(arithmetic_overflow)]

fn main() {
    match std::env::args().nth(1).as_deref() {
        Some("byte") => {
            let value = std::hint::black_box(i8::MIN);
            std::hint::black_box(-value);
        }
        Some("wide") => {
            let value = std::hint::black_box(i128::MIN);
            std::hint::black_box(-value);
        }
        Some("explicit") => {
            let value: i128 = -(-170141183460469231731687303715884105728i128);
            std::hint::black_box(value);
        }
        Some("inferred") => {
            let value: i128 = -(-170141183460469231731687303715884105728);
            std::hint::black_box(value);
        }
        _ => {
            let executable = std::env::current_exe().unwrap();
            for mode in ["byte", "wide", "explicit", "inferred"] {
                let result = std::process::Command::new(&executable).arg(mode).output().unwrap();
                assert!(!result.status.success(), "negation unexpectedly succeeded: {mode}");
                assert!(String::from_utf8_lossy(&result.stderr).contains("negate with overflow"));
            }
        }
    }
}
