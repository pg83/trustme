//@ compile-flags: --emit=metadata
//@ compile-fail: evaluation panicked during constant evaluation
#![feature(pattern_types, pattern_type_macro, generic_pattern_types)]

struct WideStart<const START: u128>(std::pat::pattern_type!(u8 is START..));

const _: () = assert!(
    std::mem::size_of::<Option<WideStart<18446744073709551616u128>>>() == 1
);

fn main() {}
