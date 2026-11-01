//@ aux-build: projection_bound_diamond.rs
use projection_bound_diamond::{Further, Value};

fn combine<I: Further>(byte: I::Cell<u8>, text: I::Cell<String>) -> (I::Cell<u8>, I::Cell<u8>, bool) {
    let copy = byte;
    (byte, copy, text == text)
}

fn main() {
    assert_eq!(combine::<Value>(42, String::from("ready")), (42, 42, true));
}
