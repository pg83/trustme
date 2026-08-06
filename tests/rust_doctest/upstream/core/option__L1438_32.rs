// Extracted from library/core/src/option.rs:1438
#![allow(unused)]
fn main() {
    let mut x = Some(4);
    match x.iter_mut().next() {
        Some(v) => *v = 42,
        None => {},
    }
    assert_eq!(x, Some(42));
    
    let mut x: Option<u32> = None;
    assert_eq!(x.iter_mut().next(), None);
}
