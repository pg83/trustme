// Extracted from library/core/src/option.rs:1878
#![allow(unused)]
fn main() {
    let mut x = Some(2);
    let old = x.replace(5);
    assert_eq!(x, Some(5));
    assert_eq!(old, Some(2));
    
    let mut x = None;
    let old = x.replace(3);
    assert_eq!(x, Some(3));
    assert_eq!(old, None);
}
