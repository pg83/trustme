//@ compile-fail: No applicable methods
struct Pairing;
impl Pairing {
    fn combine<T>(&self, first: T, _second: T) -> T { first }
}
fn main() {
    let _ = Pairing.combine(13u16, 17u32);
}
