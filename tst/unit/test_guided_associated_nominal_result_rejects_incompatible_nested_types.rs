//@ compile-fail: Type mismatch
struct Holder<T>(T);
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl Produce for &u8 { type Output = Holder<u8>; fn produce(self) -> Self::Output { Holder(3u8) } }
fn main() { let input = 91u8; let _: Holder<u16> = (&input).produce(); }
