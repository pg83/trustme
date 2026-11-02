//@ compile-fail: Duplicate specification of option `noreturn`
fn main() {
    unsafe { core::arch::asm!("ud2", options(noreturn, noreturn)); }
}
