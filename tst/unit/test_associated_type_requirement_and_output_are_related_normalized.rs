//@ run-pass
// rayon 1.5.3 `InterleaveShortest<I, J> { interleave: Interleave<Take<I>, Take<J>> }` with
// `#[derive(Debug)]`: the field's `&Interleave<Take<I>, Take<J>>` goes to `&dyn Debug`, and
// `Interleave`'s derived `Debug` needs `Take<J>: IndexedParallelIterator<Item = <Take<I> as
// IndexedParallelIterator>::Item>` from `J: IndexedParallelIterator<Item = I::Item>`.
use std::fmt::Debug;

pub trait IndexedParallelIterator {
    type Item;
}

#[derive(Debug, Clone)]
pub struct Take<I> {
    base: I,
    n: usize,
}

impl<I: IndexedParallelIterator> IndexedParallelIterator for Take<I> {
    type Item = I::Item;
}

#[derive(Debug, Clone)]
pub struct Interleave<I, J>
where
    I: IndexedParallelIterator,
    J: IndexedParallelIterator<Item = I::Item>,
{
    i: I,
    j: J,
}

#[derive(Debug, Clone)]
pub struct InterleaveShortest<I, J>
where
    I: IndexedParallelIterator,
    J: IndexedParallelIterator<Item = I::Item>,
{
    interleave: Interleave<Take<I>, Take<J>>,
}

#[derive(Debug, Clone)]
pub struct Range(u32);

impl IndexedParallelIterator for Range {
    type Item = u32;
}

fn main() {
    let v = InterleaveShortest { interleave: Interleave { i: Take { base: Range(1), n: 2 }, j: Take { base: Range(3), n: 4 } } };
    let text = format!("{:?}", v);
    assert!(text.contains("InterleaveShortest"));
    assert!(text.contains("n: 4"));
}
