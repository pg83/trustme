//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn width<const N: usize>(_: ()) -> usize {
    N
}

fn require_sized<T: Sized>() {}

fn probe<const N: usize>()
where
    [(); width::<N>(())]: Sized,
    [(); width::<N>({})]: Sized,
{
    require_sized::<[(); width::<N>({})]>();
}

fn main() {
    probe::<3>();
}
