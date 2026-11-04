//@ compile-variant: -O
#[inline(never)]
fn maybe_increment(flag: usize, value: &mut u32) {
    unsafe {
        core::arch::asm!("test {flag}, {flag}", "jnz {selected}",
            flag = in(reg) flag, selected = label { *value += 17; return; },
            options(nostack));
    }
}
fn main() {
    let mut value = 337;
    maybe_increment(std::hint::black_box(0), &mut value);
    assert_eq!(value, 337);
    maybe_increment(std::hint::black_box(3), &mut value);
    assert_eq!(value, 354);
}
