//@ compile-variant: -O
#![feature(cfg_version, generic_const_parameter_types, adt_const_params, more_qualified_paths)]
#![cfg_attr(version("1.96"), feature(const_param_ty_trait))]
#![cfg_attr(not(version("1.96")), feature(unsized_const_params))]
#![allow(incomplete_features)]

use std::marker::{ConstParamTy_, PhantomData};

struct Record<T> { value: T }
struct Source<T: ConstParamTy_, const VALUE: T>(PhantomData<T>);
trait Project { type Output; fn build() -> Self::Output; }
impl<T: ConstParamTy_, const VALUE: T> Project for Source<T, VALUE> {
    type Output = Record<T>;
    fn build() -> Self::Output { Record { value: VALUE } }
}

fn read<T: ConstParamTy_, const VALUE: T>() -> T {
    let <Source<T, VALUE> as Project>::Output { value } = Source::<T, VALUE>::build();
    value
}

fn main() {
    assert_eq!(read::<u16, 1061>(), 1061u16);
    assert!(read::<bool, true>());
    assert_eq!(read::<char, 'Б'>(), 'Б');
}
