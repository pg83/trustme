//@ compile-fail: Type mismatch
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl Produce for &u8 { type Output = *const u8; fn produce(self) -> Self::Output { self as *const u8 } }
fn main() { let input = 91u8; let _: *const u16 = (&input).produce(); }
