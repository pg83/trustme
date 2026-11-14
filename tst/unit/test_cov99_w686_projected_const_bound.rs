//@ crate-type: lib

pub trait Tag<const N: usize> {}

pub trait Source {
    type Item: Tag<7>;
}

fn select<const N: usize, T: Tag<N>>(_: &T) -> usize {
    N
}

pub fn projected<S: Source>(item: &S::Item) -> usize {
    select(item)
}
