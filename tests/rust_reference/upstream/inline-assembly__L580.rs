// Extracted from src/inline-assembly.md:580
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    struct Foo;
    let x: Foo = Foo;
    // Complex types like structs are not allowed
    unsafe { core::arch::asm!("/* {} */", in(reg) x); }
    // ERROR: cannot use value of type `Foo` for inline assembly
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
