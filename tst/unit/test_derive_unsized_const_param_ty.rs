#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]

use std::marker::UnsizedConstParamTy;

#[derive(PartialEq, Eq, std::marker::UnsizedConstParamTy)]
struct Wrapper<T>(T);

fn requires_unsized_const_param_ty<T: UnsizedConstParamTy + ?Sized>() {}

fn main() {
    requires_unsized_const_param_ty::<Wrapper<[u8; 4]>>();
}
