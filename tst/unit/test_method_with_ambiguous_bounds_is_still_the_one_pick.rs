/* rayon's `check_fold_chunks_len` / `tests/debug.rs`: `(0..8).into_par_iter().fold_chunks_with(2,
   0, sum)` with the generic `fn sum<T, U>(x: T, y: U) -> T where T: Add<U, Output = T>`, and
   `v.par_iter().fold_chunks_with(3, 0, |x, _| x)` - the method's `T` comes from the literal
   `0` and `F: Fn(T, Self::Item) -> T` from the function item or the closure. */
use std::ops::Add;

trait ParIter: Sized {
    type Item;
    fn fold_chunks_with<T, F>(self, chunk_size: usize, init: T, fold_op: F) -> FoldChunksWith<Self, T, F>
    where
        T: Send + Clone,
        F: Fn(T, Self::Item) -> T + Send + Sync,
    {
        FoldChunksWith { base: self, chunk_size, init, fold_op }
    }
}

struct FoldChunksWith<I, T, F> {
    base: I,
    chunk_size: usize,
    init: T,
    fold_op: F,
}

impl<I: ParIter, T, F> FoldChunksWith<I, T, F> {
    fn len(&self) -> usize {
        self.chunk_size
    }
}

struct Range(i32, i32);
impl ParIter for Range {
    type Item = i32;
}

struct SliceIter<'a, T>(&'a [T]);
impl<'a, T: Sync> ParIter for SliceIter<'a, T> {
    type Item = &'a T;
}

fn sum<T, U>(x: T, y: U) -> T
where
    T: Add<U, Output = T>,
{
    x + y
}

fn main() {
    assert_eq!(2, Range(0, 8).fold_chunks_with(2, 0, sum).len());
    let v: Vec<i32> = vec![1, 2, 3];
    assert_eq!(3, SliceIter(&v).fold_chunks_with(3, 0, |x, _| x).len());
    assert_eq!(1, SliceIter(&v).fold_chunks_with(1, 0, sum).len());
}
