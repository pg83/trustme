//@ compile-fail: asm! label operand requires a block
fn main() { unsafe { core::arch::asm!("jmp {}", label 7); } }
