//@ compile-variant: -O
static ON_JUMP: u16 = 53;
#[inline(never)]
fn select(flag: u64) -> u16 {
    let mut result = 11;
    unsafe {
        std::arch::asm!("test {flag}, {flag}", "jz {jump}", flag = in(reg) flag, jump = label { result = ON_JUMP; }, options(nomem, nostack));
    }
    result
}
fn main() {
    assert_eq!(select(0), 53);
    assert_eq!(select(1), 11);
}
