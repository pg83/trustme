// Extracted from library/core/src/iter/traits/iterator.rs:87
#![allow(unused)]
#![feature(iter_next_chunk)]
fn main() {
    
    let mut iter = "lorem".chars();
    
    assert_eq!(iter.next_chunk().unwrap(), ['l', 'o']);              // N is inferred as 2
    assert_eq!(iter.next_chunk().unwrap(), ['r', 'e', 'm']);         // N is inferred as 3
    assert_eq!(iter.next_chunk::<4>().unwrap_err().as_slice(), &[]); // N is explicitly 4
}
