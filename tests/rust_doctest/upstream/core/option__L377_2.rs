// Extracted from library/core/src/option.rs:377
#![allow(unused)]
fn main() {
    assert!(None < Some(0));
    assert!(Some(0) < Some(1));
}
