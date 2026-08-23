//@ crate-type: lib
// The declaration bound is valid for every Iterator::Item. A projection in a
// trait argument is resolved after this early signature well-formedness pass.

struct WithCount;

trait CountItem<T> {}

impl<T> CountItem<T> for WithCount {}

struct CoalesceBy<I, C>
where
    I: Iterator,
    C: CountItem<I::Item>,
{
    iter: I,
}

type DedupWithCount<I> = CoalesceBy<I, WithCount>;

trait Itertools: Iterator {
    fn dedup_with_count(self) -> DedupWithCount<Self>
    where
        Self: Sized,
    {
        CoalesceBy { iter: self }
    }
}
