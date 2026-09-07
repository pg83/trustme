// Under `min_generic_const_args` a const argument that is a bare path is a path
// constant, resolved in the enclosing item with its where-clauses (upstream
// `lower_const_path_to_const_arg`: `ConstArgKind::Path`), not an anonymous
// constant of its own, which would see the impls alone and fail to find
// `T: Tr<bool>` for `<T as Tr<bool>>::SIZE`.
//
// Same shape as the upstream test const-generics/mgca/assoc-const.rs.
#![feature(min_generic_const_args)]
#![allow(incomplete_features)]

pub trait Tr<X> {
    #[type_const]
    const SIZE: usize;
}

impl Tr<bool> for u8 {
    const SIZE: usize = 3;
}

fn mk_array<T: Tr<bool>>(_x: T) -> [(); <T as Tr<bool>>::SIZE] {
    [(); T::SIZE]
}

fn main() {
    assert_eq!(mk_array(0u8).len(), 3);
}
