//@ compile-fail: Type mismatch
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl Produce for &u8 { type Output = (u8, u16); fn produce(self) -> Self::Output { (1u8, 2u16) } }
fn main() { let input = 91u8; let _: (u8, u32) = (&input).produce(); }
