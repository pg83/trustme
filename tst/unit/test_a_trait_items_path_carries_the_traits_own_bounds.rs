// typewit's tests write `<_ as HasTypeWitness<TypeEq<i8, i8>>>::WITNESS` with
// `trait HasTypeWitness<W: TypeWitnessTypeArg<Arg = Self>>`. Upstream registers
// an associated item's predicates when the path is checked, and those of an
// item in a trait start with the trait's own (`predicates_of` takes the
// parent's, rustc_hir_analysis/src/collect/predicates_of.rs): `W: WitnessArg<Arg
// = Self>` is what makes `Self = i8`. We registered `?S: Has<Same<i8, i8>>` and
// the item's own bounds only, and an unknown self never gets an impl.
use std::marker::PhantomData;

pub trait WitnessArg {
    type Arg: ?Sized;
}

pub struct Same<L, R>(PhantomData<(L, R)>);

impl<L, R> WitnessArg for Same<L, R> {
    type Arg = L;
}

pub trait Make: WitnessArg {
    const MAKE: Self;
}

impl<L> Make for Same<L, L> {
    const MAKE: Self = Same(PhantomData);
}

pub trait Has<W: WitnessArg<Arg = Self>> {
    const WITNESS: W;
}

impl<T, W: Make<Arg = T>> Has<W> for T {
    const WITNESS: W = W::MAKE;
}

fn assert_type<T, U>(_: T) {}

fn main() {
    assert_type::<_, Same<i8, i8>>(<_ as Has<Same<i8, i8>>>::WITNESS);
}
