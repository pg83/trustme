//@ compile-fail: Too many const parameters
struct Measure;
impl Measure { fn width<T, const N: usize>(&self, _: &[T; N]) -> usize { N } }
fn main() { let _ = Measure.width::<u8, 2, 3>(&[5, 7]); }
