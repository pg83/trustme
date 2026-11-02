//@ compile-fail: Duplicate specification of option `pure`
fn main() {
    let value: u64; unsafe { core::arch::asm!("mov {0}, 13", lateout(reg) value, options(pure, pure, nomem)); }
}
