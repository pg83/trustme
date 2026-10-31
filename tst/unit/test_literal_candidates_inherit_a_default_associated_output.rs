#![feature(specialization)]
#![allow(incomplete_features)]
trait Convert<T> { type Output: Default; fn code() -> u16; }
struct Queue;
impl<T> Convert<T> for Queue { default type Output = u16; default fn code() -> u16 { 1 } }
impl Convert<i32> for Queue { fn code() -> u16 { 32 } }
impl Convert<i64> for Queue { fn code() -> u16 { 64 } }
fn output<T>(_: T) -> (<Queue as Convert<T>>::Output, u16) where Queue: Convert<T> {
    (Default::default(), <Queue as Convert<T>>::code())
}
fn main() {
    let (value, code): (u16, u16) = output(9);
    assert_eq!(value, 0);
    assert_eq!(code, 32);
}
