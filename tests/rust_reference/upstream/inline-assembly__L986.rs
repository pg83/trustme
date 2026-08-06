// Extracted from src/inline-assembly.md:986
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    extern "C" fn foo() -> i32 { 0 }
    
    let z: i32;
    // To call a function, we have to inform the compiler that we're clobbering
    // callee saved registers
    unsafe { core::arch::asm!("call {}", sym foo, out("rax") z, clobber_abi("C")); }
    assert_eq!(z, 0);
    }
}
