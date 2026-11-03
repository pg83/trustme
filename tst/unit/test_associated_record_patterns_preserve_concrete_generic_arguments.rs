//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
trait Project { type Output; }
struct Identity<T>(T);
impl<T> Project for Identity<T> { type Output = Record<T>; }
fn build<T>(value: T) -> <Identity<T> as Project>::Output {
    <Identity<T> as Project>::Output { value }
}
fn main() {
    let <Identity<u16> as Project>::Output { value } = build(809u16);
    assert_eq!(value, 809);
    let <Identity<char> as Project>::Output { value } = build('Ж');
    assert_eq!(value, 'Ж');
}
