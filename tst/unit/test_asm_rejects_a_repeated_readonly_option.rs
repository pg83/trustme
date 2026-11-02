//@ compile-fail: Duplicate specification of option `readonly`
fn main() {
    unsafe { core::arch::asm!("nop", options(readonly, readonly)); }
}
