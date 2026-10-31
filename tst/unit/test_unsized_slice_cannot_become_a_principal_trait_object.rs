//@ compile-fail: Type mismatch
trait Length { fn length(&self) -> usize; }
impl Length for [u8] { fn length(&self) -> usize { self.len() } }
fn main() { let values = [17u8, 23]; let slice: &[u8] = &values; let object: &dyn Length = slice; let _ = object; }
