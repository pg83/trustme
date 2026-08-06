// Extracted from library/core/src/bool.rs:16
#![allow(unused)]
fn main() {
    assert_eq!(false.then_some(0), None);
    assert_eq!(true.then_some(0), Some(0));
}
