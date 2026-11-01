//@ compile-flags: -Znext-solver=globally
#![feature(specialization, generic_arg_infer)]
#![allow(incomplete_features)]

trait Pick { fn code() -> u8; }
impl<A, B> Pick for (A, B) { default fn code() -> u8 { 1 } }
impl<T> Pick for (T, T) { fn code() -> u8 { 2 } }

fn select<A, B>(_: A, _: B) -> u8 where (A, B): Pick {
    <(A, B) as Pick>::code()
}

fn main() {
    assert_eq!(select(1, 2i8), 1);
    assert_eq!(select(1i8, 2), 1);
    assert_eq!(select(1.0, 2.0f32), 1);
    assert_eq!(select(1.0f32, 2.0), 1);
    let equal = |left: [u8; _], right: [u8; 2]| select(left, right);
    let unequal = |left: [u8; _], right: [u8; 2]| select(left, right);
    assert_eq!(equal([1, 2], [3, 4]), 2);
    assert_eq!(unequal([1, 2, 3], [4, 5]), 1);
    let right_equal = |left: [u8; 2], right: [u8; _]| select(left, right);
    let right_unequal = |left: [u8; 2], right: [u8; _]| select(left, right);
    assert_eq!(right_equal([1, 2], [3, 4]), 2);
    assert_eq!(right_unequal([1, 2], [3, 4, 5]), 1);
}
