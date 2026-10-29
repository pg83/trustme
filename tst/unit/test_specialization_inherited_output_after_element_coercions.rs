#![feature(specialization)]
#![allow(incomplete_features)]
trait Read { type Value; fn read(&self) -> Self::Value; }
impl<T: ?Sized> Read for T {
    type Value = usize;
    default fn read(&self) -> usize { 1 }
}
impl<T> Read for &[T] { fn read(&self) -> usize { self.len() + 10 } }
fn inspect<T: Read>(values: &[T]) -> T::Value { values[0].read() }
fn main() {
    let a = [1u8, 2];
    let b = [3u8, 4, 5];
    let c = [6u8, 7, 8, 9];
    let result = inspect(&[&a[..], &b, &c]);
    assert_eq!(result, 12);
}
