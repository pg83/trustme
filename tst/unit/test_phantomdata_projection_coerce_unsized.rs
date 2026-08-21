//@ crate-type: lib

#![feature(coerce_unsized, dispatch_from_dyn, unsize)]

use std::marker::{PhantomData, Unsize};
use std::ops::{CoerceUnsized, DispatchFromDyn};

trait Mirror {
    type Assoc;
}

impl<T> Mirror for T {
    type Assoc = T;
}

struct Wrapper<T: 'static> {
    value: &'static T,
    marker: <PhantomData<T> as Mirror>::Assoc,
}

impl<T, U> CoerceUnsized<Wrapper<U>> for Wrapper<T> where T: Unsize<U> {}
impl<T, U> DispatchFromDyn<Wrapper<U>> for Wrapper<T> where T: Unsize<U> {}

struct TupleWrapper<T: 'static>(
    &'static T,
    <PhantomData<T> as Mirror>::Assoc,
);

impl<T, U> CoerceUnsized<TupleWrapper<U>> for TupleWrapper<T> where T: Unsize<U> {}
impl<T, U> DispatchFromDyn<TupleWrapper<U>> for TupleWrapper<T> where T: Unsize<U> {}
