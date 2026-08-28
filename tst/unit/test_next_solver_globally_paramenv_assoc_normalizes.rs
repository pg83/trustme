//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// A ParamEnv candidate whose associated value is an unnormalised projection
// (`<I as II>::Item`) must relate to the required type through
// normalisation in the current env (AliasRelate), so the dead SpecExtend
// candidate dies with NoSolution and the &T one is selected uniquely.
// Mirrors liballoc vec_deque spec_extend.

#![feature(specialization)]
#![allow(incomplete_features)]

trait It {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}

trait Trusted: It {}

struct VInto<T>(Option<T>);

impl<T> It for VInto<T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        self.0.take()
    }
}

impl<T> Trusted for VInto<T> {}

struct SIter<'a, T>(&'a T);

impl<'a, T> It for SIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<&'a T> {
        Some(self.0)
    }
}

impl<'a, T> Trusted for SIter<'a, T> {}

trait II {
    type Item;
    type IntoIter: It<Item = Self::Item>;
    fn into_iter2(self) -> Self::IntoIter;
}

trait Allocator {}

struct VD<T, A> {
    x: Option<T>,
    allocator: A,
}

trait SpecExtend<T, I> {
    fn spec_extend(&mut self, iter: I);
}

impl<T, I, A: Allocator> SpecExtend<T, I> for VD<T, A>
where
    I: It<Item = T>,
{
    default fn spec_extend(&mut self, mut iter: I) {
        self.x = iter.next();
    }
}

impl<T, I, A: Allocator> SpecExtend<T, I> for VD<T, A>
where
    I: Trusted<Item = T>,
{
    default fn spec_extend(&mut self, mut iter: I) {
        self.x = iter.next();
    }
}

impl<T, A: Allocator> SpecExtend<T, VInto<T>> for VD<T, A> {
    fn spec_extend(&mut self, mut iter: VInto<T>) {
        self.x = iter.next();
    }
}

impl<'a, T: 'a, I, A: Allocator> SpecExtend<&'a T, I> for VD<T, A>
where
    I: It<Item = &'a T>,
    T: Copy,
{
    fn spec_extend(&mut self, mut iter: I) {
        self.x = iter.next().copied();
    }
}

impl<'a, T: 'a, A: Allocator> SpecExtend<&'a T, SIter<'a, T>> for VD<T, A>
where
    T: Copy,
{
    fn spec_extend(&mut self, mut iter: SIter<'a, T>) {
        self.x = iter.next().copied();
    }
}

trait Extend2<A> {
    fn extend2<I: II<Item = A>>(&mut self, iter: I);
}

impl<'a, T: 'a + Copy, A: Allocator> Extend2<&'a T> for VD<T, A> {
    fn extend2<I: II<Item = &'a T>>(&mut self, iter: I) {
        self.spec_extend(iter.into_iter2());
    }
}
