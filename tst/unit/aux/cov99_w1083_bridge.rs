#![no_std]

pub fn type_identity<const N: usize>(
    value: cov99_w1083_shapes::AliasType<N>,
) -> cov99_w1083_shapes::AliasType<N> {
    value
}

pub fn trait_identity<const N: usize>(
    value: cov99_w1083_shapes::AliasTrait<N>,
) -> cov99_w1083_shapes::AliasTrait<N> {
    value
}
