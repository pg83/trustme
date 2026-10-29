//@ compile-fail: using both label and output operands for inline assembly is unstable
fn main() {
    let mut value = 0u64;
    unsafe {
        std::arch::asm!("add {value}, 1", "jmp {done}", value = inout(reg) value,
            done = label {}, options(nostack));
    }
    assert_eq!(value, 1);
}
