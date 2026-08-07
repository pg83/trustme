// Extracted from library/core/src/ops/mod.rs:84
#![allow(unused)]
fn main() {
    fn call_with_one<F>(func: F) -> usize
        where F: Fn(usize) -> usize
    {
        func(1)
    }

    let double = |x| x * 2;
    assert_eq!(call_with_one(double), 2);
}
