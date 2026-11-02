//@ compile-fail: invalid register `rsp`
fn main() { unsafe { core::arch::asm!("nop", in("rsp") 7u64); } }
