//@ compile-variant: -O
#[inline(never)]
fn route<const OFFSET: u64>(value: u64) -> u64 {
    let mut result = OFFSET;
    unsafe {
        std::arch::asm!("add {result}, 3", result = inout(reg) result, options(nomem, nostack));
        std::arch::asm!("test {value}, {value}", "jnz {nonzero}", value = in(reg) value,
            nonzero = label { result *= 2; }, options(nomem, nostack));
    }
    result
}
fn main() {
    assert_eq!(route::<5>(0), 8);
    assert_eq!(route::<5>(9), 16);
    assert_eq!(route::<11>(0), 14);
    assert_eq!(route::<11>(1), 28);
}
