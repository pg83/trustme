//@ aux-build: driverless_repetition_exports.rs
//@ reject-flags: --edition=2021 --cfg invoke => error || error
use driverless_repetition_exports::choose_tokens;

fn main() {
    #[cfg(invoke)]
    choose_tokens!(invalid);
    assert_eq!(choose_tokens!(value), 31u8);
}
