//@ compile-fail: error:0: asm! format string index out of range

fn main() {
    unsafe {
        core::arch::asm!("/* {4294967296} */", in(reg) 977u32);
    }
}
