//@ aux-build: parameterized_signal_for_path_checks.rs
//@ compile-fail: Unexpected enum in path
extern crate parameterized_signal_for_path_checks;

fn main() {
    let _ = parameterized_signal_for_path_checks::Signal::<u8>::Value::missing();
}
