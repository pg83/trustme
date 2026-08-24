//@ crate-type: lib

trait ParallelIterator: Sized {
    type Item;

    fn map<F, R>(self, map: F) -> Map<Self, F>
    where
        F: Fn(Self::Item) -> R,
    {
        Map(self, map)
    }
}

trait Consumer<T> {
    type Result;
}

trait IndexedParallelIterator: ParallelIterator {
    fn drive<C>(self, consumer: C) -> C::Result
    where
        C: Consumer<Self::Item>;
}

struct Map<I, F>(I, F);

impl<I, F, R> ParallelIterator for Map<I, F>
where
    I: ParallelIterator,
    F: Fn(I::Item) -> R,
{
    type Item = R;
}

impl<I, F, R> IndexedParallelIterator for Map<I, F>
where
    I: IndexedParallelIterator,
    F: Fn(I::Item) -> R,
{
    fn drive<C>(self, _consumer: C) -> C::Result
    where
        C: Consumer<Self::Item>,
    {
        loop {}
    }
}

struct MultiZip<A>(A);

impl<A> ParallelIterator for MultiZip<(A,)>
where
    A: IndexedParallelIterator,
{
    type Item = (A::Item,);
}

impl<A> IndexedParallelIterator for MultiZip<(A,)>
where
    A: IndexedParallelIterator,
{
    fn drive<C>(self, consumer: C) -> C::Result
    where
        C: Consumer<Self::Item>,
    {
        self.0.0.map(|item| (item,)).drive(consumer)
    }
}
