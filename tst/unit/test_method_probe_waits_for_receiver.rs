// A candidate that only matches because the receiver is not known yet cannot
// be picked: which one is right is decided by the type, so the probe waits for
// it. `Box<Box<[T]>>` has no `IntoIterator` impl of its own, so the call goes
// through `&Box<[T]>` once the type arrives.
//@ edition: 2018

use std::rc::Rc;
use std::slice::Iter;
use std::vec::IntoIter;

fn main() {
    let boxed_slice = vec![0; 10].into_boxed_slice();

    let _: Iter<'_, i32> = boxed_slice.into_iter();
    let _: Iter<'_, i32> = Box::new(boxed_slice.clone()).into_iter();
    let _: Iter<'_, i32> = Rc::new(boxed_slice.clone()).into_iter();

    let _: IntoIter<i32> = IntoIterator::into_iter(boxed_slice);

    // A lone candidate is the answer whatever the receiver becomes.
    let v = Vec::new();
    let mut w = v.clone();
    w.push(1u8);
    assert_eq!(w.len(), 1);
}
