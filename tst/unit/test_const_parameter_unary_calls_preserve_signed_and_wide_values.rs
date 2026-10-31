#![feature(cfg_version, generic_const_parameter_types, adt_const_params)]
#![cfg_attr(version("1.96"), feature(const_param_ty_trait))]
#![cfg_attr(not(version("1.96")), feature(unsized_const_params))]
#![allow(incomplete_features)]
fn invert<T: std::marker::ConstParamTy_ + std::ops::Not<Output = T>, const N: T>() -> T { !N }
fn negate<T: std::marker::ConstParamTy_ + std::ops::Neg<Output = T>, const N: T>() -> T { -N }
fn forward<T: std::marker::ConstParamTy_ + std::ops::Not<Output = T>, const N: T>() -> T { invert::<T, N>() }
fn main() {
    assert_eq!(forward::<u128, 0x173b_2547_91ab_cdef_0123_4567_89ab_cdef>(), !0x173b_2547_91ab_cdef_0123_4567_89ab_cdefu128);
    assert_eq!(forward::<i128, -37>(), 36);
    assert_eq!(negate::<i128, { i128::MAX }>(), -i128::MAX);
    assert_eq!((negate::<i8, -37>(), negate::<isize, 41>()), (37, -41));
}
