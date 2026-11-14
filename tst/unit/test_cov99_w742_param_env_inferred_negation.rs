//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub fn negate_join<T>(choose_mut: bool, value: &mut T) -> T
where
    for<'a> &'a T: core::ops::Neg<Output = T>,
{
    let result = -(if choose_mut { &mut *value } else { &*value });
    result
}
