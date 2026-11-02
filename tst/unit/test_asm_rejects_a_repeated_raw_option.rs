//@ compile-fail: Duplicate specification of option `raw`
fn main() {
    unsafe { core::arch::asm!("nop", options(raw, raw)); }
}
