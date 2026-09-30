// num-modular's `FixedMersenne<const P: u8, const K: umax>()` is built with
// `Self {}`. A braced literal names a tuple struct's fields by index, and
// with no fields there is nothing to name: rustc accepts it (an unnamed field
// left out would be E0063). We asserted that a tuple struct literal has
// values or a base.
pub struct Fixed<const P: u8, const K: u64>();
impl<const P: u8, const K: u64> Fixed<P, K> {
    pub const MODULUS: u64 = (1u64 << P) - K;
    fn new() -> Self {
        Self {}
    }
    fn modulus(&self) -> u64 {
        Self::MODULUS
    }
}
pub struct Empty();
fn main() {
    assert_eq!(Fixed::<5, 1>::new().modulus(), 31);
    let Empty {} = Empty {};
    let _ = Empty();
}
