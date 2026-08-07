// Extracted from library/core/src/borrow.rs:194
#![allow(unused)]
fn main() {
    use std::borrow::BorrowMut;

    fn check<T: BorrowMut<[i32]>>(mut v: T) {
        assert_eq!(&mut [1, 2, 3], v.borrow_mut());
    }

    let v = vec![1, 2, 3];

    check(v);
}
