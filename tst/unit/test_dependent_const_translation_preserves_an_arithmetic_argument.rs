#![feature(adt_const_params, generic_const_parameter_types, generic_const_exprs, unsized_const_params)]
#![allow(incomplete_features)]

use std::marker::{ConstParamTy_, PhantomData};

struct Ready<T: ConstParamTy_, const VALUE: T>(PhantomData<T>);

fn fetch<T: ConstParamTy_ + Copy, const VALUE: T>() -> T {
    VALUE
}

fn adjusted<const N: u16>() -> u16
where
    Ready<u16, { N + 3 }>: Sized,
{
    fetch::<u16, { N + 3 }>()
}

fn main() {
    assert_eq!(adjusted::<14>(), 17);
    assert_eq!(adjusted::<1006>(), 1009);
}
