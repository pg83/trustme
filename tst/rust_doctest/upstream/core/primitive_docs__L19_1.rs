// Extracted from library/core/src/primitive_docs.rs:19
#![allow(unused)]
fn main() {
    let bool_val = true & false | false;
    assert!(!bool_val);
}
