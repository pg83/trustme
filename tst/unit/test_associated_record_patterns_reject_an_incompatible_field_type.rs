//@ compile-fail: Type mismatch
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
trait Project { type Output; }
struct Identity<T>(T);
impl<T> Project for Identity<T> { type Output = Record<T>; }
fn main() {
    let <Identity<u16> as Project>::Output { value } = Record { value: true };
    let _: bool = value;
}
