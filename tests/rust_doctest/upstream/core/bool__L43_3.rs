// Extracted from library/core/src/bool.rs:43
#![allow(unused)]
fn main() {
    assert_eq!(false.then(|| 0), None);
    assert_eq!(true.then(|| 0), Some(0));
}
