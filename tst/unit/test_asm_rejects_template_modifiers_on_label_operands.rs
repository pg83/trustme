//@ compile-fail: invalid asm template modifier
fn main() {
    unsafe { core::arch::asm!("jmp {chosen:x}", chosen = label { return; }, options(noreturn)); }
}
