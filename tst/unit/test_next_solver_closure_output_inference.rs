//@ check-pass
//@ compile-flags: -Znext-solver

use std::fmt::Debug;

trait LocalIterator {
    type Item;
}

struct Source;

impl LocalIterator for Source {
    type Item = usize;
}

struct Map<I, F>(I, F);

impl<B, I, F> LocalIterator for Map<I, F>
where
    I: LocalIterator,
    F: FnMut(I::Item) -> B,
{
    type Item = B;
}

trait LocalIntoIterator {
    type Item;
}

impl<I: LocalIterator> LocalIntoIterator for I {
    type Item = I::Item;
}

fn consume_debug_entries<D, I>(_entries: I)
where
    D: Debug,
    I: LocalIntoIterator<Item = D>,
{
}

struct Mask<T, const N: usize>(T);

impl<T, const N: usize> Mask<T, N> {
    fn test(&self, index: usize) -> bool {
        index < N
    }
}

impl<T: Debug, const N: usize> Debug for Mask<T, N> {
    fn fmt(&self, _f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        consume_debug_entries(Map(Source, |index| self.test(index)));
        Ok(())
    }
}

fn main() {}
