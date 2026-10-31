//@ compile-fail: Type mismatch
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl Produce for &u8 { type Output = [u8; 2]; fn produce(self) -> Self::Output { [9u8; 2] } }
fn main() { let input = 91u8; let _: [u16; 2] = (&input).produce(); }
