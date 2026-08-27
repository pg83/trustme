//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// A specialising concrete head can match an unnormalised associated type
// only after that projection normalises. It must not shadow the proven
// blanket impl and constrain the rigid projection to its concrete iterator.
// Mirrors VecDeque::extend in liballoc.

#![feature(specialization)]
#![allow(incomplete_features)]

trait It {
    type Item;
}

trait IntoIt {
    type Item;
    type IntoIter: It<Item = Self::Item>;
    fn into_it(self) -> Self::IntoIter;
}

struct Deque<T>(Option<T>);
struct Exact<T>(Option<T>);

impl<T> It for Exact<T> {
    type Item = T;
}

trait SpecExtend<T, I> {
    fn spec_extend(&mut self, iter: I);
}

impl<T, I> SpecExtend<T, I> for Deque<T>
where
    I: It<Item = T>,
{
    default fn spec_extend(&mut self, _iter: I) {}
}

impl<T> SpecExtend<T, Exact<T>> for Deque<T> {
    fn spec_extend(&mut self, _iter: Exact<T>) {}
}

trait Extend<T> {
    fn extend<I: IntoIt<Item = T>>(&mut self, iter: I);
}

impl<T> Extend<T> for Deque<T> {
    fn extend<I: IntoIt<Item = T>>(&mut self, iter: I) {
        self.spec_extend(iter.into_it());
    }
}
