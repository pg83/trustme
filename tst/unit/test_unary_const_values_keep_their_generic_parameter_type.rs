#![feature(cfg_version, generic_const_parameter_types, adt_const_params)]
#![cfg_attr(version("1.96"), feature(const_param_ty_trait))]
#![cfg_attr(not(version("1.96")), feature(unsized_const_params))]
#![allow(incomplete_features)]
fn invert<T: std::marker::ConstParamTy_ + std::ops::Not<Output = T>, const VALUE: T>() -> T { !VALUE }
fn main() {
    assert!(!invert::<bool, true>());
    assert!(invert::<bool, false>());
    assert_eq!(invert::<u8, 37>(), 218);
    assert_eq!(invert::<u16, 37>(), 65498);
}
