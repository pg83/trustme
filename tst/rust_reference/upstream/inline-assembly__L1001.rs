// Extracted from src/inline-assembly.md:1001
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    extern "sysv64" fn foo() -> i32 { 0 }
    extern "win64" fn bar(x: i32) -> i32 { x + 1 }
    
    let z: i32;
    // We can even call multiple functions with different conventions and
    // different saved registers
    unsafe {
        core::arch::asm!(
            "call {}",
            "mov ecx, eax",
            "call {}",
            sym foo,
            sym bar,
            out("rax") z,
            clobber_abi("sysv64"),
            clobber_abi("win64"),
        );
    }
    assert_eq!(z, 1);
    }
}
