// Extracted from library/alloc/src/string.rs:3468
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("abc");
    let mut drain = s.drain(..);
    assert_eq!(drain.as_str(), "abc");
    let _ = drain.next().unwrap();
    assert_eq!(drain.as_str(), "bc");
}
