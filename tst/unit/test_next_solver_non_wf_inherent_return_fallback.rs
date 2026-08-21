//@ crate-type: lib
//@ compile-flags: -Znext-solver

use std::ops::Deref;

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
}

fn use_deref_fallback<T>(list: &List<T>) {
    let _ = list.iter();
}
