// Extracted from library/std/src/keyword_docs.rs:1089
#![allow(unused)]
fn main() {
    // Taking a mutable reference.
    fn push_two(v: &mut Vec<u8>) {
        v.push(2);
    }
    
    // A mutable reference cannot be taken to a non-mutable variable.
    let mut v = vec![0, 1];
    // Passing a mutable reference.
    push_two(&mut v);
    
    assert_eq!(v, vec![0, 1, 2]);
}
