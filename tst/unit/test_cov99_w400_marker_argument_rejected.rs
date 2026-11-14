//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Too many type parameters passed to

trait Accept<T: ?Sized> {}

pub fn rejected_marker_argument<T>()
where
    (): Accept<dyn Send<T>>,
{}
