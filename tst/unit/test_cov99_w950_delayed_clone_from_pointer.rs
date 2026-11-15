//@ compile-flags: -Copt-level=0

use std::marker::PhantomData;

struct Deferred<T>(PhantomData<T>);

impl<T: Clone> Deferred<T> {
    const REPLACE: fn(&mut T, &T) = <T as Clone>::clone_from;
}

#[inline(never)]
fn replace<T: Clone>(destination: &mut T, source: &T) {
    (Deferred::<T>::REPLACE)(destination, source);
}

fn main() {
    let mut destination = (3_u32, 7_u32);
    let source = (11_u32, 19_u32);
    replace(&mut destination, &source);
    assert_eq!(destination, source);
}
