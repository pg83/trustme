//@ compile-fail: Duplicate specification of option `preserves_flags`
fn main() {
    unsafe { core::arch::asm!("nop", options(preserves_flags, preserves_flags)); }
}
