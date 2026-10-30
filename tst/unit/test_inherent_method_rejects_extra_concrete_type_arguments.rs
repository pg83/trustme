//@ compile-fail: Too many type parameters
struct Measure;
impl Measure { fn identity<T>(&self, value: T) -> T { value } }
fn main() { let _ = Measure.identity::<u8, u16>(7); }
