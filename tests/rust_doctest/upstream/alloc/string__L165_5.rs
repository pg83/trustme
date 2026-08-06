// Extracted from library/alloc/src/string.rs:165
#![allow(unused)]
extern crate alloc;
fn main() {
    // The first byte is 104 - the byte value of `'h'`
    let s = "hello";
    assert_eq!(s.as_bytes()[0], 104);
    // or
    assert_eq!(s.as_bytes()[0], b'h');
    
    // The first byte is 240 which isn't obviously useful
    let s = "💖💖💖💖💖";
    assert_eq!(s.as_bytes()[0], 240);
}
