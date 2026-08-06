// Extracted from library/alloc/src/str.rs:305
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "foo foo 123 foo";
    assert_eq!("new new 123 foo", s.replacen("foo", "new", 2));
    assert_eq!("faa fao 123 foo", s.replacen('o', "a", 3));
    assert_eq!("foo foo new23 foo", s.replacen(char::is_numeric, "new", 1));
}
