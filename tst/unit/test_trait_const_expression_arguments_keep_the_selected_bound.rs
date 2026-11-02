//@ compile-variant: -O
trait Width<const N: usize> { fn width() -> usize { N } }
impl Width<5> for u8 {}
fn measure<T: Width<{ 2 + 3 }>>() -> usize { T::width() }
fn main() { assert_eq!(measure::<u8>(), 5); }
