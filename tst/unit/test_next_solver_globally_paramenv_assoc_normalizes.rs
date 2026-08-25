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

trait II {
    type Item;
    type IntoIter: It<Item = Self::Item>;
    fn into_iter2(self) -> Self::IntoIter;
}

struct VD<T> {
    x: Option<T>,
}

trait SpecExtend<T, I> {
    fn spec_extend(&mut self, iter: I);
}

impl<T, I> SpecExtend<T, I> for VD<T>
where
    I: It<Item = T>,
{
    default fn spec_extend(&mut self, mut iter: I) {
        self.x = iter.next();
    }
}

impl<'a, T: 'a, I> SpecExtend<&'a T, I> for VD<T>
where
    I: It<Item = &'a T>,
    T: Copy,
{
    fn spec_extend(&mut self, mut iter: I) {
        self.x = iter.next().copied();
    }
}

trait Extend2<A> {
    fn extend2<I: II<Item = A>>(&mut self, iter: I);
}

impl<'a, T: 'a + Copy> Extend2<&'a T> for VD<T> {
    fn extend2<I: II<Item = &'a T>>(&mut self, iter: I) {
        self.spec_extend(iter.into_iter2());
    }
}
