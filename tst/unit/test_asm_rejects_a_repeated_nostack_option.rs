//@ compile-fail: Duplicate specification of option `nostack`
fn main() {
    unsafe { core::arch::asm!("nop", options(nostack, nostack)); }
}
