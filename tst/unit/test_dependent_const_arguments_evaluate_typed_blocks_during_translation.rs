#![feature(adt_const_params, generic_const_parameter_types, generic_const_exprs)]
#![allow(incomplete_features)]
//@ compile-fail: error
use std::marker::{ConstParamTy_, PhantomData};

struct Witness<T: ConstParamTy_, const VALUE: T>(PhantomData<T>);

fn value<T: ConstParamTy_ + Copy, const VALUE: T>() -> T {
    VALUE
}

fn forward<T: ConstParamTy_ + Copy, const VALUE: T>() -> T
where
    Witness<T, { let selected = VALUE; selected }>: Sized,
{
    value::<T, { let selected = VALUE; selected }>()
}

fn main() {
    assert_eq!(forward::<u16, 173>(), 173);
    assert_eq!(forward::<i16, { -29 }>(), -29);
    assert!(forward::<bool, true>());
}
