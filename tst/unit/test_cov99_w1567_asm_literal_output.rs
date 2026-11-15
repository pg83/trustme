//@ compile-fail: LValue expected, got RValue
//@ compile-flags: -Cpanic=abort

fn main() {
    unsafe {
        core::arch::asm!("/* {0} */", out(reg) 1u64);
    }
}
