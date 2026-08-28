//@ crate-type: lib
//@ compile-flags: -Znext-solver

use std::ops::Deref;

trait SliceFallback {
    fn bounded(&self) -> usize;
}

impl<T> SliceFallback for [T] {
    fn bounded(&self) -> usize {
        self.len()
    }
}

trait Project {
    type Output;
    fn project(self) -> Self::Output;
}

impl Project for u8 {
    type Output = u16;

    fn project(self) -> Self::Output {
        self.into()
    }
}

struct List<T>(T);

impl<'a, T: Copy> IntoIterator for &'a List<T> {
    type Item = T;
    type IntoIter = std::iter::Empty<T>;

    fn into_iter(self) -> Self::IntoIter {
        std::iter::empty()
    }
}

impl<T> Deref for List<T> {
    type Target = [T];

    fn deref(&self) -> &[T] {
        std::slice::from_ref(&self.0)
    }
}

impl<T> List<T> {
    fn iter(&self) -> <&Self as IntoIterator>::IntoIter
    where
        T: Copy,
    {
        std::iter::empty()
    }

    fn bounded(&self) -> usize
    where
        T: Copy,
    {
        0
    }

    fn project<M: Project>(&self, value: M) -> M::Output {
        value.project()
    }
}

fn use_deref_fallback<T>(list: &List<T>) {
    let _ = list.iter();
    let _: usize = list.bounded();
}

fn use_method_parameter_bound(list: &List<u8>) -> u16 {
    list.project(1_u8)
}
