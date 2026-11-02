//@ compile-fail: Duplicate specification of option `att_syntax`
fn main() {
    unsafe { core::arch::asm!("nop", options(att_syntax, att_syntax)); }
}
