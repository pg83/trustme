// Extracted from src/inline-assembly.md:408
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    extern "C" fn foo() {
        println!("Hello from inline assembly")
    }
    // `sym` can be used to refer to a function (even if it doesn't have an
    // external name we can directly write)
    unsafe { core::arch::asm!("call {}", sym foo, clobber_abi("C")); }
    }
}
