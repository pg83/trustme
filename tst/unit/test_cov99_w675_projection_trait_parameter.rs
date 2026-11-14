//@ crate-type: lib
//@ compile-flags: --emit=metadata

trait Project<P> {
    type Item;
}

trait Select<T> {}

fn require<T, P>()
where
    (): Select<<T as Project<P>>::Item>,
    T: Project<P>,
{}

fn probe<T, P>()
where
    (): Select<<T as Project<P>>::Item>,
    T: Project<P>,
{
    require::<T, _>();
}
