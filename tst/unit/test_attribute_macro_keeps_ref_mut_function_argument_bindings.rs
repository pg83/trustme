//@ proc-macro-aux-build: preserve_mutable_argument_pattern.rs
extern crate preserve_mutable_argument_pattern;

use preserve_mutable_argument_pattern::preserve;

#[preserve]
fn change(ref mut value: u32, ref increment: u32) -> u32 {
    *value += *increment;
    *value
}

fn main() {
    assert_eq!(change(41, 13), 54);
    assert_eq!(change(67, 0), 67);
}
