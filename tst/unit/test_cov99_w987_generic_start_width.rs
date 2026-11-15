//@ compile-flags: --emit=metadata
//@ compile-fail: evaluation panicked during constant evaluation
#![feature(pattern_types)]

type WideStart<const START: u128> = std::pat::pattern_type!(u8 is START..);

const _: () = assert!(
    std::mem::size_of::<Option<WideStart<18446744073709551616>>>() == 1
);

fn main() {}
