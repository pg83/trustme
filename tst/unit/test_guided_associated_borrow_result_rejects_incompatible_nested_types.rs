//@ compile-fail: Type mismatch
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl Produce for &u8 { type Output = &'static u8; fn produce(self) -> Self::Output { &5u8 } }
fn main() { let input = 91u8; let _: &'static u16 = (&input).produce(); }
