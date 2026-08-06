// Extracted from library/alloc/src/vec/drain.rs:49
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec!['a', 'b', 'c'];
    let mut drain = vec.drain(..);
    assert_eq!(drain.as_slice(), &['a', 'b', 'c']);
    let _ = drain.next().unwrap();
    assert_eq!(drain.as_slice(), &['b', 'c']);
}
