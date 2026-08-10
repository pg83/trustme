// Extracted from src/inline-assembly.md:1643
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    let bytes: *const u8;
    let len: usize;
    unsafe {
        core::arch::asm!(
            "jmp 3f", "2: .ascii \"Hello World!\"",
            "3: lea {bytes}, [2b+rip]",
            "mov {len}, 12",
            bytes = out(reg) bytes,
            len = out(reg) len
        );
    }
    
    let s = unsafe { core::str::from_utf8_unchecked(core::slice::from_raw_parts(bytes, len)) };
    
    assert_eq!(s, "Hello World!");
    }
}
