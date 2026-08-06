// Extracted from src/inline-assembly.md:555
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    extern "C" fn foo() {}
    
    // Integers are allowed...
    let y: i64 = 5;
    unsafe { core::arch::asm!("/* {} */", in(reg) y); }
    
    // and pointers...
    let py = &raw const y;
    unsafe { core::arch::asm!("/* {} */", in(reg) py); }
    
    // floats as well...
    let f = 1.0f32;
    unsafe { core::arch::asm!("/* {} */", in(xmm_reg) f); }
    
    // even function pointers and simd vectors.
    let func: extern "C" fn() = foo;
    unsafe { core::arch::asm!("/* {} */", in(reg) func); }
    
    let z = unsafe { core::arch::x86_64::_mm_set_epi64x(1, 0) };
    unsafe { core::arch::asm!("/* {} */", in(xmm_reg) z); }
    }
}
