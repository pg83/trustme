// Extracted from src/inline-assembly.md:1180
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let x: i64 = 0;
    let z: i64;
    // Same exception applies as with nomem.
    unsafe {
        core::arch::asm!("push {x}", "add qword ptr [rsp], 1", "pop {x}",
            x = inout(reg) x => z,
            options(readonly)
        );
    }
    assert_eq!(z, 1);
    }
}
