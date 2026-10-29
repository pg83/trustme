#![feature(auto_traits, negative_impls)]
auto trait Mobile {}
struct Blocked(u8);
impl !Mobile for Blocked {}
struct Packet<T>(T);
trait Select { fn select(self) -> u8; }
impl<T: Mobile> Select for &&Packet<T> {
    fn select(self) -> u8 { 1 }
}
impl<T> Select for &Packet<T> {
    fn select(self) -> u8 { 2 }
}
fn main() {
    let good = Packet([2u16, 3, 5]);
    let bad = Packet(Blocked(7));
    assert_eq!((&&good).select(), 1);
    assert_eq!((&&bad).select(), 2);
    assert_eq!(good.0.iter().sum::<u16>(), 10);
    assert_eq!(bad.0.0, 7);
}
