//@ edition: 2015
macro_rules! define {
    ($ty:ty) => {
        fn measure(value: &$ty) -> usize { value.len() }
    };
}
trait Measure { fn len(&self) -> usize; }
impl Measure for [u8; 3] { fn len(&self) -> usize { 3 } }
define!(dyn Measure);
fn main() { assert_eq!(measure(&[19u8, 23, 29]), 3); }
