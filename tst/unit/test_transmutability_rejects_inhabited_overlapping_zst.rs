//@ compile-fail: TransmuteFrom

#![feature(transmutability)]

fn assert_transmutable<T>()
where
    Never: std::mem::TransmuteFrom<T>,
{
}

enum Never {}

enum Source {
    First(u8, ()),
    Second((), u16),
}

fn main() {
    assert_transmutable::<Source>();
}
