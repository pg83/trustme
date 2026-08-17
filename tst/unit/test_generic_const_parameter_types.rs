// A const parameter's own type may name the parameters declared before it, and
// the value's type is only known once those are.
#![feature(adt_const_params, unsized_const_params, generic_const_parameter_types)]
#![allow(incomplete_features)]

use std::marker::ConstParamTy_;

fn value<T: ConstParamTy_, const N: usize, const M: [T; N]>() -> [T; N] {
    M
}

fn ignored<U, T: ConstParamTy_, const N: usize, const M: [T; N]>(_: U) -> [T; N] {
    M
}

struct Holder<const N: usize, const M: [u8; N]>;

fn held<const N: usize, const M: [u8; N]>(_: Holder<N, M>) -> [u8; N] {
    M
}

fn main() {
    assert_eq!(value::<u8, 2, { [12; 2] }>(), [12, 12]);
    // The repeat count is inferred from the parameter's type.
    assert_eq!(value::<u8, 3, { [7; _] }>(), [7, 7, 7]);
    assert_eq!(ignored::<bool, u16, 2, { [9; _] }>(true), [9, 9]);
    assert_eq!(held(Holder::<2, { [1; 2] }>), [1, 1]);
    assert_eq!(held::<_, _>(Holder::<4, { [3; 4] }>), [3, 3, 3, 3]);
}
