// Extracted from library/alloc/src/string.rs:122
#![allow(unused)]
extern crate alloc;
fn main() {
    // `s` is ASCII which represents each `char` as one byte
    let s = "hello";
    assert_eq!(s.len(), 5);
    
    // A `char` array with the same contents would be longer because
    // every `char` is four bytes
    let s = ['h', 'e', 'l', 'l', 'o'];
    let size: usize = s.into_iter().map(|c| size_of_val(&c)).sum();
    assert_eq!(size, 20);
    
    // However, for non-ASCII strings, the difference will be smaller
    // and sometimes they are the same
    let s = "💖💖💖💖💖";
    assert_eq!(s.len(), 20);
    
    let s = ['💖', '💖', '💖', '💖', '💖'];
    let size: usize = s.into_iter().map(|c| size_of_val(&c)).sum();
    assert_eq!(size, 20);
}
