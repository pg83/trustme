// `for<T> T: Trait` quantifies a where predicate over a type that exists only
// inside it. Nothing here models such a predicate, so it is parsed and dropped
// -- previously the binder's name was left to resolve on its own and could not.
//
// Same shape as the upstream tests traits/non_lifetime_binders/basic.rs and
// sized-late-bound-issue-114872.rs.
#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Trait {}

impl<T> Trait for T {}

fn quantified()
where
    for<T> T: Trait,
{
}

fn overTuple<T>()
where
    for<U> (T, U): Copy,
{
}

fn sizedBinder()
where
    for<V> V: Sized,
{
}

// A lifetime binder in the same position is unchanged.
fn lifetimeBinder<F>(_: F)
where
    F: for<'a> Fn(&'a u32) -> &'a u32,
{
}

fn main() {
    // The quantified functions are only declared. A bound that ranges over
    // every type cannot be satisfied, so rustc rejects naming them too -- this
    // test is about the declarations parsing and resolving.
    lifetimeBinder(|x| x);
    assert_eq!(1, 1);
}
