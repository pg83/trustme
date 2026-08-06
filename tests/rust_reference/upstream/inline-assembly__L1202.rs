// Extracted from src/inline-assembly.md:1202
fn main() -> ! {
#[cfg(target_arch = "x86_64")] {
    // We can use an instruction to trap execution inside of a noreturn block
    unsafe { core::arch::asm!("ud2", options(noreturn)); }
}
#[cfg(not(target_arch = "x86_64"))] panic!("no return");
}
