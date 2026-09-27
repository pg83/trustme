//@ crate-type: lib
// rust-lang/rust#106423 (ui const-generics/generic_const_exprs): the caller's
// where-clauses `[(); N * 2 + 1]: Sized` and `[(); N * 2]: Sized` name the
// const parameter N, so they are not global, and `zpk2tf_st`'s
// `[(); {N * 2} + 1]: Sized` cannot be equated with either. rustc's
// `winnow_candidates` takes a trivial builtin candidate (`BuiltinCandidate {
// has_nested: false }` - an array is Sized with no nested obligation) before
// any where-clause; we let the ambiguous where-clauses stand next to it.
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
#![allow(unused)]

use core::mem::MaybeUninit;

pub struct Arr<T, const N: usize> {
    v: [MaybeUninit<T>; N],
}

impl<T, const N: usize> Arr<T, N> {
    const ELEM: MaybeUninit<T> = MaybeUninit::uninit();
    const INIT: [MaybeUninit<T>; N] = [Self::ELEM; N]; // important for optimization of `new`

    fn new() -> Self {
        Arr { v: Self::INIT }
    }
}

pub struct BaFormatFilter<const N: usize> {}

pub enum DigitalFilter<const N: usize>
where
    [(); N * 2 + 1]: Sized,
    [(); N * 2]: Sized,
{
    Ba(BaFormatFilter<{ N * 2 + 1 }>),
}

pub fn iirfilter_st_copy<const N: usize, const M: usize>(_: [f32; M]) -> DigitalFilter<N>
where
    [(); N * 2 + 1]: Sized,
    [(); N * 2]: Sized,
{
    let zpk = zpk2tf_st(&Arr::<f32, { N * 2 }>::new(), &Arr::<f32, { N * 2 }>::new());
    DigitalFilter::Ba(zpk)
}

pub fn zpk2tf_st<const N: usize>(_z: &Arr<f32, N>, _p: &Arr<f32, N>) -> BaFormatFilter<{ N + 1 }>
where
    [(); N + 1]: Sized,
{
    BaFormatFilter {}
}
