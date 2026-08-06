// Extracted from src/inline-assembly.md:425
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // swizzle [0, 1, 2, 3] => [3, 2, 0, 1]
    const SHUFFLE: u8 = 0b01_00_10_11;
    let x: core::arch::x86_64::__m128 = unsafe { core::mem::transmute([0u32, 1u32, 2u32, 3u32]) };
    let y: core::arch::x86_64::__m128;
    // Pass a constant value into an instruction that expects an immediate like `pshufd`
    unsafe {
        core::arch::asm!("pshufd {xmm}, {xmm}, {shuffle}",
            xmm = inlateout(xmm_reg) x=>y,
            shuffle = const SHUFFLE
        );
    }
    let y: [u32; 4] = unsafe { core::mem::transmute(y) };
    assert_eq!(y, [3, 2, 0, 1]);
    }
}
