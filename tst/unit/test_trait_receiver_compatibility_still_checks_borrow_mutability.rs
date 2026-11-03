//@ compile-fail: Argument 1 mismatch
trait Read { fn read(&self) -> u32; }
struct Value(u32);
impl Read for Value {
    fn read(&mut self) -> u32 { self.0 }
}
fn main() {}
