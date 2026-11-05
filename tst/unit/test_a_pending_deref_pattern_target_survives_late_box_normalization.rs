//@ aux-build: stalled_pattern_lenses.rs
//@ compile-variant: -O
#![feature(deref_patterns)]
#![allow(incomplete_features)]
extern crate stalled_pattern_lenses as lenses;

trait Factory: Sized {
    type Output;
    fn make(self) -> Self::Output;
}
impl Factory for i32 {
    type Output = Box<u8>;
    fn make(self) -> Self::Output { Box::new(17) }
}
impl Factory for i64 {
    type Output = lenses::Lens<u8>;
    fn make(self) -> Self::Output { lenses::scalar::<u8>() }
}
fn produce<T: Factory>(value: T) -> T::Output { value.make() }

fn main() {
    assert!(matches!(produce(0), deref!(17u8)));
}
