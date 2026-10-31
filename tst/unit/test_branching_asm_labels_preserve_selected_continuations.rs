#[inline(never)]
fn choose(flag: bool) -> u32 {
    unsafe {
        if flag {
            std::arch::asm!("jmp {exit}", exit = label { return 37; }, options(noreturn, nostack));
        } else {
            std::arch::asm!("jmp {exit}", exit = label { return 41; }, options(noreturn, nostack));
        }
    }
}
fn main() {
    assert_eq!(choose(std::hint::black_box(true)), 37);
    assert_eq!(choose(std::hint::black_box(false)), 41);
}
