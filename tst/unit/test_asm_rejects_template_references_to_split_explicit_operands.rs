//@ compile-fail: explicit register arguments cannot be referenced
fn main() {
    let output: u32;
    unsafe { core::arch::asm!("add {0:e}, 5", inout("eax") 317u32 => output); }
    assert_eq!(output, 322);
}
