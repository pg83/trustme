//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn fixed<const N: usize>() -> [u8; const { N + 1 }]
where
    [(); const { N + 1 }]:,
{
    [13, 17, 19]
}

fn main() {
    let _ = fixed::<2>();
}
