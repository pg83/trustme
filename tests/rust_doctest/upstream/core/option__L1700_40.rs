// Extracted from library/core/src/option.rs:1700
#![allow(unused)]
fn main() {
    let mut opt = None;
    let val = opt.insert(1);
    assert_eq!(*val, 1);
    assert_eq!(opt.unwrap(), 1);
    let val = opt.insert(2);
    assert_eq!(*val, 2);
    *val = 3;
    assert_eq!(opt.unwrap(), 3);
}
