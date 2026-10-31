//@ compile-fail: Type mismatch
struct Buffer<T: ?Sized>(T);
trait Marker { fn value(&self) -> u8 { 17 } }
impl<T: ?Sized> Marker for Buffer<T> {}
fn main() { let sized = Buffer([17u8, 23]); let source: &Buffer<[u8]> = &sized; let object: &dyn Marker = source; let _ = object; }
