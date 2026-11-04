//@ compile-variant: -O
#[inline(never)]
fn exit_if_requested(flag: usize) {
    unsafe {
        core::arch::asm!("test {flag}, {flag}", "jnz {stop}",
            flag = in(reg) flag, stop = label { std::process::exit(73); },
            options(nostack));
    }
}
fn main() {
    exit_if_requested(std::hint::black_box(0));
    assert_eq!(std::hint::black_box(353u16).wrapping_add(6), 359);
}
