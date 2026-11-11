#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {}

fn main() {
    unsafe {
        let expected = std::arch::x86_64::__cpuid_count(0, 0);
        let (mut a, mut b, mut c, mut d) = (0u32, 0u32, 0u32, 0u32);
        llvm_asm!(
            "cpuid\n"
            : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
            : "{eax}"(0u32), "{ecx}"(0u32)
            : "rbx", "memory", "cc"
            : "volatile"
        );
        assert_eq!((a, b, c, d), (expected.eax, expected.ebx, expected.ecx, expected.edx));
    }
}
