//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn seven<T>() -> usize {
    7
}

struct Probe;

impl Probe {
    fn value<const N: usize>(&self) -> usize {
        N
    }
}

fn ignored_type<T>() -> usize
where
    [(); seven::<T>()]:,
{
    Probe.value::<{ seven::<T>() }>()
}

fn main() {
    assert_eq!(ignored_type::<u8>(), 7);
}
