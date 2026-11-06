trait Leaf: Copy { const MARK: u8; }
impl Leaf for u16 { const MARK: u8 = 19; }
struct Holder<T>(T);
trait Packet {
    type Output;
    const STAMP: u8;
    fn unpack(self) -> Self::Output;
}
impl<T: Leaf> Packet for Holder<T> {
    type Output = T;
    const STAMP: u8 = T::MARK;
    fn unpack(self) -> T { self.0 }
}
fn unpack<P: Packet>(value: P, _: P::Output) -> (P::Output, u8) {
    (value.unpack(), P::STAMP)
}
trait Depth { const VALUE: usize; }
struct End;
impl Depth for End { const VALUE: usize = 0; }
struct Link<T>(T);
impl<T: Depth> Depth for Link<T> { const VALUE: usize = T::VALUE + 1; }
fn depth<T: Depth>() -> usize { T::VALUE }
fn main() {
    assert_eq!(unpack(Holder(Default::default()), 31u16), (0u16, 19));
    assert_eq!(depth::<Link<Link<Link<End>>>>(), 3);
}
