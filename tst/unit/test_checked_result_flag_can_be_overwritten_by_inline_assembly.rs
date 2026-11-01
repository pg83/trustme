//@ compile-variant: -O
#![feature(core_intrinsics)]
#[inline(never)]
fn overwritten_flag() -> bool {
    let mut pair = (0i32, false);
    let flag = &raw mut pair.1;
    pair = std::intrinsics::add_with_overflow(17i32, 0i32);
    unsafe { std::arch::asm!("mov byte ptr [{flag}], 1", flag = in(reg) flag, options(nostack)); }
    if pair.1 { true } else { false }
}
fn main() { assert!(overwritten_flag()); }
