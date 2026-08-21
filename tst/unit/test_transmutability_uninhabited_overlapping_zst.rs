#![feature(transmutability)]

fn assert_transmutable<T>()
where
    (): std::mem::TransmuteFrom<T>,
{
}

enum Never {}

enum Source {
    First(u8, Never),
    Second(Never, u16),
}

fn main() {
    assert_transmutable::<Source>();
}
