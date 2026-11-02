//@ compile-fail: Duplicate specification of option `nomem`
fn main() {
    unsafe { core::arch::asm!("nop", options(nomem, nomem)); }
}
