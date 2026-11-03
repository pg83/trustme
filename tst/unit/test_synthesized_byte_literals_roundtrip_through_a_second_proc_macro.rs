//@ proc-macro-aux-build: synthesized_byte_roundtrip.rs
extern crate synthesized_byte_roundtrip;
use synthesized_byte_roundtrip::echo;
fn main() {
    let value = synthesized_byte_roundtrip::produce!();
    assert_eq!(value, &[0, 34, 92, 255]);
    assert_eq!(value.len(), 4);
}
