fn unconditional() -> u32 {
    unsafe { std::arch::asm!("jmp {exit}", exit = label { return 37; }, options(noreturn, nostack)); }
}
fn conditional(flag: usize) -> u32 {
    unsafe { std::arch::asm!("test {flag}, {flag}", "jnz {exit}", flag = in(reg) flag, exit = label { return 41; }, options(nostack)); }
    17
}
fn mixed(flag: usize) -> u32 {
    let mut value = 17;
    unsafe {
        std::arch::asm!(
            "test {flag}, {flag}", "jnz {returning}", "jmp {continuing}",
            flag = in(reg) flag,
            returning = label { return 41; },
            continuing = label { value = 53; },
            options(noreturn, nostack),
        );
    }
    value
}
fn main() {
    assert_eq!((unconditional(), conditional(0), conditional(1)), (37, 17, 41));
    assert_eq!((mixed(0), mixed(1)), (53, 41));
}
