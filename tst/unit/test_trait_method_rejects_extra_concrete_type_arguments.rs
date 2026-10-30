//@ compile-fail: Too many type parameters
struct Measure;
trait Identity { fn identity<T>(&self, value: T) -> T; }
impl Identity for Measure { fn identity<T>(&self, value: T) -> T { value } }
fn main() { let _ = Measure.identity::<u8, u16>(7); }
