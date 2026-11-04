//@ compile-fail: explicit register arguments cannot have names
fn main() {
    let output: u32;
    unsafe { core::arch::asm!("add {slot:e}, 7", slot = inout("eax") 331u32 => output); }
    assert_eq!(output, 338);
}
