#![feature(generic_const_items)]
#![allow(incomplete_features)]

trait Owner {
    const VALUE<const N: u32>: u32;
}

impl Owner for () {
    const VALUE<const N: u32>: u32 = N;
}

fn main() {
    assert_eq!(<() as Owner>::VALUE::<7>, 7);
}
