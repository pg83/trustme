//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

struct Value<T>(T);

impl<T> Value<T> {
    const ZERO: T = unsafe { std::mem::zeroed() };
}

pub fn is_zero(value: u8) -> bool {
    match value {
        Value::<u8>::ZERO => true,
        _ => false,
    }
}
