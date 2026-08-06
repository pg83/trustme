// Extracted from library/core/src/slice/mod.rs:4559
#![allow(unused)]
fn main() {
    let mut slice: &[_] = &['a', 'b', 'c'];
    let first = slice.split_off_first().unwrap();
    
    assert_eq!(slice, &['b', 'c']);
    assert_eq!(first, &'a');
}
