//@ compile-only
#![feature(pattern_types)]

struct Upper<const END: u128>(std::pat::pattern_type!(u8 is ..=END));

fn main() {
    let _ = std::mem::size_of::<Upper<18446744073709551616>>();
}
