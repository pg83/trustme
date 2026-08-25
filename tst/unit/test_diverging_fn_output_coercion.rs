use std::marker::PhantomData;

struct Empty<T>(PhantomData<T>);

fn empty<T>() -> Empty<T> {
    Empty(PhantomData)
}

struct Filter<I, P>(I, P);

trait ParallelIterator: Sized + Send {
    type Item: Send;

    fn filter<P>(self, filter_op: P) -> Filter<Self, P>
    where
        P: Fn(&Self::Item) -> bool + Sync + Send,
    {
        Filter(self, filter_op)
    }

    fn collect<C>(self) -> C
    where
        C: FromParallelIterator<Self::Item>,
    {
        C::from_par_iter(self)
    }
}

impl<T: Send> ParallelIterator for Empty<T> {
    type Item = T;
}

impl<I, P> ParallelIterator for Filter<I, P>
where
    I: ParallelIterator,
    P: Fn(&I::Item) -> bool + Sync + Send,
{
    type Item = I::Item;
}

trait IntoParallelIterator {
    type Iter: ParallelIterator<Item = Self::Item>;
    type Item: Send;

    fn into_par_iter(self) -> Self::Iter;
}

impl<I: ParallelIterator> IntoParallelIterator for I {
    type Iter = I;
    type Item = I::Item;

    fn into_par_iter(self) -> I {
        self
    }
}

trait FromParallelIterator<T: Send> {
    fn from_par_iter<I: IntoParallelIterator<Item = T>>(iter: I) -> Self;
}

impl<T: Send> FromParallelIterator<T> for Vec<T> {
    fn from_par_iter<I: IntoParallelIterator<Item = T>>(_iter: I) -> Self {
        Vec::new()
    }
}

fn main() {
    let values: Vec<i32> = empty().filter(|_| unreachable!()).collect();
    assert!(values.is_empty());
}
