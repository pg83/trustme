// Extracted from src/inline-assembly.md:1220
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")]
    let _: () = unsafe {
        // You may still jump to a `label` block
        core::arch::asm!("jmp {}", label {
            println!();
        }, options(noreturn));
    };
}
