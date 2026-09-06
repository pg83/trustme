//@ run-pass
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
// `Choose<NonCopy>::Result`: two inherent impls of `Choose<_>` define `Result`,
// and only their where-clauses tell them apart.  Upstream keeps a candidate
// only if the impl's bounds select without error, so `impl<T: Copy>` is out
// for `NonCopy` and the projection is `()`; for `&str` it is `Vec<&str>`.
struct Choose<T>(T);
struct NonCopy;

impl<T: Copy> Choose<T> {
    type Result = Vec<T>;
}

impl Choose<NonCopy> {
    type Result = ();
}

fn main() {
    let _: Choose<NonCopy>::Result = ();
    let v: Choose<&str>::Result = vec!["x"];
    assert_eq!(v.len(), 1);
}
