//@ crate-type: lib

trait Trait
where
    for<'a> Self::Gat<'a>: OtherTrait,
    for<'a, 'b, 'c> <Self::Gat<'a> as OtherTrait>::OtherGat<'b>: HigherRanked<'c>,
{
    type Gat<'a>;
}

trait OtherTrait {
    type OtherGat<'b>;
}

trait HigherRanked<'c> {}

fn needs_bound<T: for<'b, 'c> OtherTrait<OtherGat<'b>: HigherRanked<'c>>>() {}

fn test<T: Trait>() {
    needs_bound::<T::Gat<'_>>();
}
