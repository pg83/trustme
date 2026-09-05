#![feature(min_specialization, rustc_attrs)]

// Guard for the specialization relation itself: an impl specializes another
// when the other's where-clauses hold for everything the first applies to,
// proven with the first impl's own bounds assumed.  Here the parent asks
// `T: PartialEq<Other>`, which the child does not spell out - it follows from
// the child's `T: Fast<U>` through `Fast`'s supertrait.  `core` relies on the
// same shape for `[T; N] == [U; N]` over `BytewiseEq`.


#[rustc_specialization_trait]
trait Fast<Rhs = Self>: PartialEq<Rhs> + Sized {}
impl Fast for u64 {}

trait SpecEq<Other> {
    fn spec_eq(a: &Self, b: &Other) -> u8;
}

impl<T: PartialEq<Other>, Other> SpecEq<Other> for T {
    default fn spec_eq(_: &T, _: &Other) -> u8 {
        1
    }
}

impl<T: Fast<U>, U> SpecEq<U> for T {
    fn spec_eq(_: &T, _: &U) -> u8 {
        2
    }
}

fn main() {
    assert_eq!(<u64 as SpecEq<u64>>::spec_eq(&1, &1), 2);
    assert_eq!(<u32 as SpecEq<u32>>::spec_eq(&1, &1), 1);
}
