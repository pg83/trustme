// An array literal under an array (or slice) expectation checks each element with the
// expected element type and coerces it there (rustc `check_expr_array`, `CoerceMany`):
// `[id(LazyJust::new(|| Bound::Unbounded))]` returned as `[LazyJust<Bound<u8>, fn() -> Bound<u8>>; 1]`
// hands the calls the expected output whose expected input gives the closure its fn pointer;
// a fresh element variable instead let the closure bind `F` before the expectation reached it.
// (the array counterpart of proptest's `prop_oneof!` tuple case)
use std::marker::PhantomData;
use std::sync::Arc;

pub struct LazyJust<T, F: Fn() -> T> {
    function: F,
    _t: PhantomData<T>,
}

impl<T, F: Fn() -> T> LazyJust<T, F> {
    pub fn new(function: F) -> Self {
        LazyJust { function, _t: PhantomData }
    }

    pub fn call(&self) -> T {
        (self.function)()
    }
}

pub type LazyJustFn<T> = LazyJust<T, fn() -> T>;

pub struct TupleUnion<T>(T);

impl<T> TupleUnion<T> {
    pub fn new(t: T) -> Self {
        TupleUnion(t)
    }
}

type WA<S> = (u32, Arc<S>);

pub enum Bound<A> {
    Included(A),
    Excluded(A),
    Unbounded,
}

fn id<T>(t: T) -> T {
    t
}

fn make() -> [LazyJustFn<Bound<u8>>; 1] {
    [id(LazyJust::new(|| Bound::Unbounded))]
}

fn main() {
    let u = make();
    let b = u[0].call();
    assert!(matches!(b, Bound::Unbounded));
}
