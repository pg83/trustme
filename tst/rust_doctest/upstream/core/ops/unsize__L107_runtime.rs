// Extracted from library/core/src/ops/unsize.rs:107
#![allow(unused)]
#![feature(dispatch_from_dyn, unsize)]
fn main() {
    use std::{ops::DispatchFromDyn, marker::Unsize};
    struct Rc<T: ?Sized>(std::rc::Rc<T>);
    impl<T: ?Sized, U: ?Sized> DispatchFromDyn<Rc<U>> for Rc<T>
    where
        T: Unsize<U>,
    {}
}
