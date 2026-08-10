// Extracted from src/inline-assembly.md:1028
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    extern "C" fn foo(x: i32) -> i32 { 0 }
    
    let z: i32;
    // explicit registers must be used to not accidentally overlap.
    unsafe {
        core::arch::asm!(
            "mov eax, {:e}",
            "call {}",
            out(reg) z,
            sym foo,
            clobber_abi("C")
        );
        // ERROR: asm with `clobber_abi` must specify explicit registers for outputs
    }
    assert_eq!(z, 0);
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
