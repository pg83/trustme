#![feature(specialization, generic_arg_infer)]
#![allow(incomplete_features)]
trait Pick { fn code() -> u8; }
impl<A, B> Pick for (A, B) { default fn code() -> u8 { 1 } }
impl<T> Pick for (T, T) { fn code() -> u8 { 2 } }
fn select<A, B>(_: A, _: B) -> u8 where (A, B): Pick {
    <(A, B) as Pick>::code()
}
fn main() {
    assert_eq!(select(1, 2i8), 2);
    assert_eq!(select(1i8, 2), 2);
    assert_eq!(select(1.0, 2.0f32), 2);
    assert_eq!(select(1.0f32, 2.0), 2);
}
