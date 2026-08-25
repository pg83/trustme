//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// An impl parameter without a `?Sized` relaxation carries an implicit
// `Sized` requirement: the specialised `BoxIter for B<I>` impl must be
// NoSolution for `B<I: ?Sized>`, leaving the general impl as the unique
// normalisation source.  Mirrors liballoc boxed/iter.rs.

#![feature(specialization)]
#![allow(incomplete_features)]

trait It {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}

struct B<I: ?Sized> {
    p: *mut I,
}

impl<I: It + ?Sized> It for B<I> {
    type Item = I::Item;
    fn next(&mut self) -> Option<I::Item> {
        None
    }
}

trait BoxIter {
    type Item;
    fn last(self) -> Option<Self::Item>;
}

impl<I: It + ?Sized> BoxIter for B<I> {
    type Item = I::Item;
    default fn last(mut self) -> Option<I::Item> {
        self.next()
    }
}

impl<I: It> BoxIter for B<I> {
    fn last(mut self) -> Option<I::Item> {
        self.next()
    }
}

fn take_last<I: It + ?Sized>(b: B<I>) -> Option<<B<I> as It>::Item> {
    BoxIter::last(b)
}
