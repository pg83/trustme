pub trait Feed {
    const WIDTH: usize;
    fn make() -> Self;
    fn value(&self) -> u16;
}
pub struct Packet(u16);
impl Feed for Packet {
    const WIDTH: usize = 3;
    fn make() -> Self { Self(17) }
    fn value(&self) -> u16 { self.0 }
}
impl Packet { pub fn inherent() -> Self { Self(19) } }
#[inline(never)]
pub fn generic_read<T: Feed>() -> u16 {
    let value = T::make();
    T::value(&value)
}
