//@ compile-fail: Receiver type
trait Read { fn read(self: Box<Self>) -> u32; }
struct Value(u32);
impl Read for Value {
    fn read(value: Box<Self>) -> u32 { value.0 }
}
fn main() {}
