// Extracted from src/inline-assembly.md:748
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // Pointers and integers can mix (as long as they are the same size)
    let x: isize = 0;
    let y: *mut ();
    // Transmute an `isize` to a `*mut ()`, using inline assembly magic
    unsafe { core::arch::asm!("/*{}*/", inout(reg) x=>y); }
    assert!(y.is_null()); // Extremely roundabout way to make a null pointer
    }
}
