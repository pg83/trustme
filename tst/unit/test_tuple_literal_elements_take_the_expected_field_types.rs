// A tuple literal under a tuple expectation checks each element coercible to its expected
// field (rustc `check_expr_tuple`): `((1, Arc::new(LazyJust::new(|| Bound::Unbounded))),)`
// returned as `TupleUnion<(WA<LazyJust<Bound<A>, fn() -> Bound<A>>>,)>` hands the calls the
// expected output whose expected input gives the closure its fn pointer; a fresh variable
// per field instead let the closure bind `F` before the expectation reached it.
// (proptest `arbitrary/_alloc/collections.rs:258`, `prop_oneof!`)
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

fn make<A>() -> TupleUnion<(WA<LazyJustFn<Bound<A>>>,)> {
    TupleUnion::new(((1, Arc::new(LazyJust::new(|| Bound::Unbounded))),))
}

fn main() {
    let u = make::<u8>();
    let b = ((u.0).0).1.call();
    assert!(matches!(b, Bound::Unbounded));
    assert_eq!(((u.0).0).0, 1);
}
