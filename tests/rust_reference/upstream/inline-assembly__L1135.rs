// Extracted from src/inline-assembly.md:1135
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i32 = 0;
    let z: i32;
    // If we allocate our own memory, such as via `push`, however.
    // we can still use it
    unsafe {
        core::arch::asm!("push {x}", "add qword ptr [rsp], 1", "pop {x}",
            x = inout(reg) x => z,
            options(nomem)
        );
    }
    assert_eq!(z, 1);
    }
}
