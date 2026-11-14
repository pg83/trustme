//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: type annotations needed

trait Project<P> {
    type Item;
}

trait Select<T> {}

fn require<T, P>()
where
    (): Select<<T as Project<P>>::Item>,
    T: Project<P>,
{}

fn ambiguous<T>()
where
    T: Project<u8> + Project<u16>,
    (): Select<<T as Project<u8>>::Item>,
    (): Select<<T as Project<u16>>::Item>,
{
    require::<T, _>();
}
