#![feature(trivial_bounds)]

trait Family { type Item; }
struct Missing;

trait Select<T> { fn select() -> u32; }

impl<T> Select<T> for ()
where
    u16: AsRef<<Missing as Family>::Item>,
    Missing: Family,
{
    fn select() -> u32 { 0 }
}

impl Select<u8> for () {
    fn select() -> u32 { 43 }
}

fn main() {
    assert_eq!(<() as Select<u8>>::select(), 43);
}
