//@ compile-fail: Failed to find an impl
trait Twin {}
impl<F> Twin for (F,F) {}
struct Ops;
impl Ops { fn first(value:u8)->u16 { value as u16 } fn second(value:u8)->u16 { value as u16+1 } }
fn require<T:Twin>(_:T) {}
fn main() { require((Ops::first,Ops::second)); }
