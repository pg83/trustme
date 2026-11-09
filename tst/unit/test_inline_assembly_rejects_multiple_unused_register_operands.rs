//@ compile-fail: multiple unused asm arguments

fn main() {
    unsafe {
        core::arch::asm!("", in(reg) 151u64, in(reg) 157u64);
    }
}
