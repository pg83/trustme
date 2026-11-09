trait Permit<const N: usize> {}
impl Permit<41> for u8 {}

trait Read<const N: usize> {
    fn read() -> u8;
}

struct Table;
impl<const N: usize> Read<N> for Table
where
    u8: Permit<N>,
{
    fn read() -> u8 {
        43
    }
}
impl Read<37> for Table {
    fn read() -> u8 {
        47
    }
}

fn main() {
    assert_eq!(<Table as Read<41>>::read(), 43);
    assert_eq!(<Table as Read<37>>::read(), 47);
}
