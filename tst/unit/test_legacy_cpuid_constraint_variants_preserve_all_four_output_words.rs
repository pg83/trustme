#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {}

fn main() {
    let expected = unsafe { std::arch::x86_64::__cpuid_count(0, 0) };
    let expected = (expected.eax, expected.ebx, expected.ecx, expected.edx);
    let (mut a, mut b, mut c, mut d) = (0u32, 0u32, 0u32, 0u32);
    unsafe {
        llvm_asm!(
            "cpuid"
            : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
            : "{eax}"(0u32)
            : "memory"
            : "volatile"
        );
    }
    assert_eq!((a, b, c, d), expected);
    unsafe {
        llvm_asm!(
            "cpuid"
            : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
            : "0"(0u32), "2"(0u32)
            : "memory"
            : "volatile"
        );
    }
    assert_eq!((a, b, c, d), expected);
    unsafe {
        llvm_asm!(
            "cpuid"
            : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
            : "{eax}"(0u32), "{ecx}"(0u32)
            : "memory"
            : "volatile"
        );
    }
    assert_eq!((a, b, c, d), expected);
}
