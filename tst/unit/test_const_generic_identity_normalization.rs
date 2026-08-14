#![feature(generic_const_exprs)]
#![allow(incomplete_features, unused_braces)]

struct Array<const N: usize>([u8; {{ N }}])
where
    [(); {{ N }}]:;

fn make<const N: usize>() -> Array<{{ N }}>
where
    [u8; {{ N }}]:,
{
    Array([0; {{ N }}])
}

fn forward<const M: usize>() -> Array<{{ M }}> {
    make::<{{ M }}>()
}

fn main() {
    assert_eq!(forward::<2>().0, [0; 2]);
}
