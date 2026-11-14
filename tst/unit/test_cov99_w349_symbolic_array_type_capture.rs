#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn bytes<const N: usize>() -> [u8; core::mem::size_of::<[u8; N]>()]
where
    [(); core::mem::size_of::<[u8; N]>()]:,
{
    [19; core::mem::size_of::<[u8; N]>()]
}

fn main() {
    assert_eq!(bytes::<3>(), [19; 3]);
    assert_eq!(bytes::<5>(), [19; 5]);
}
