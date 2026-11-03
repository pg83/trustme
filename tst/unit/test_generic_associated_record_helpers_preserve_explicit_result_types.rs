//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
trait Project { type Output; }
struct Identity<T>(T);
impl<T> Project for Identity<T> { type Output = Record<T>; }
fn make<T>(value: T) -> <Identity<T> as Project>::Output {
    <Identity<T> as Project>::Output { value }
}
fn take<T>(record: <Identity<T> as Project>::Output) -> T {
    let <Identity<T> as Project>::Output { value } = record;
    value
}
fn main() {
    let record = make::<u16>(797);
    let answer: u16 = take::<u16>(record);
    assert!(answer == 797);
}
