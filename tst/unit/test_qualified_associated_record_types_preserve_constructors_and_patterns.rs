//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
trait Project { type Output; }
struct Identity<T>(T);
impl<T> Project for Identity<T> { type Output = Record<T>; }
fn make<T>(value: T) -> <Identity<T> as Project>::Output {
    <Identity<T> as Project>::Output { value }
}
fn take<T>(value: <Identity<T> as Project>::Output) -> T {
    let <Identity<T> as Project>::Output { value } = value;
    value
}
fn main() {
    let <Identity<u16> as Project>::Output { value } = make(761u16);
    assert_eq!(value, 761);
    assert_eq!(take::<u16>(make(769)), 769);
}
