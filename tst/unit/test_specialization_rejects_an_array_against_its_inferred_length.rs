//@ compile-fail: mismatched types
#![feature(specialization, generic_arg_infer)]
#![allow(incomplete_features)]
trait Pick { fn code() -> u8; }
impl<A, B> Pick for (A, B) { default fn code() -> u8 { 1 } }
impl<T> Pick for (T, T) { fn code() -> u8 { 2 } }
fn select<A, B>(_: A, _: B) -> u8 where (A, B): Pick {
    <(A, B) as Pick>::code()
}
fn main() {
    let inferred = |left: [u8; _], right: [u8; 2]| select(left, right);
    let _ = inferred([1, 2, 3], [4, 5]);
}
