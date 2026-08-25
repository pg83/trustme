//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// rustc specialization graph: a specialising impl that omits an associated
// item inherits the nearest ancestor's value, and projecting through it is
// legal because the ancestor declared the item final (no `default`).  Inside
// the specialised `find`, the env bound `P: FnMut(&Self::Item)` must
// normalise `<F<I> as FI<I>>::Item` to `<I as It>::Item` through the default
// impl even though the selected (specialised) impl has no `type Item`.
// Mirrors libcore's Fuse/FuseImpl pair (iter/adapters/fuse.rs).

#![feature(specialization)]
#![allow(incomplete_features)]

trait It {
    type Item;
    fn find<P>(&mut self, predicate: P) -> Option<Self::Item>
    where
        P: FnMut(&Self::Item) -> bool;
}

trait Fused: It {}

trait FI<I> {
    type Item;
    fn find<P>(&mut self, predicate: P) -> Option<Self::Item>
    where
        P: FnMut(&Self::Item) -> bool;
}

struct F<I> {
    iter: Option<I>,
}

impl<I: It> FI<I> for F<I> {
    type Item = <I as It>::Item;

    default fn find<P>(&mut self, _predicate: P) -> Option<Self::Item>
    where
        P: FnMut(&Self::Item) -> bool,
    {
        None
    }
}

impl<I: Fused> FI<I> for F<I> {
    fn find<P>(&mut self, predicate: P) -> Option<Self::Item>
    where
        P: FnMut(&Self::Item) -> bool,
    {
        self.iter.as_mut()?.find(predicate)
    }
}
