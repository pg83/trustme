//@ compile-fail: Unexpected token
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    unsafe {
        core::arch::asm!("", operand = 0);
    }
}
