//@ aux-build: parameterized_signal_for_path_checks.rs
//@ compile-fail: Type parameters were not expected here (enum params go on the variant)
extern crate parameterized_signal_for_path_checks;

fn main() {
    let _ = parameterized_signal_for_path_checks::Signal::<u8>::Value::<u16>(17);
}
