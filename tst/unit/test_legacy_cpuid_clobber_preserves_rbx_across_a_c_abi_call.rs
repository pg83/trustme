#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {}

#[inline(never)]
unsafe extern "C" fn without_b_output() -> u32 {
    let (mut a, mut c, mut d) = (0u32, 0u32, 0u32);
    llvm_asm!(
        "cpuid\n"
        : "={eax}"(a), "={ecx}"(c), "={edx}"(d)
        : "{eax}"(0u32), "{ecx}"(0u32)
        : "rbx"
        : "volatile"
    );
    a ^ c ^ d
}

#[inline(never)]
unsafe extern "C" fn with_b_output() -> u32 {
    let (mut a, mut b, mut c, mut d) = (0u32, 0u32, 0u32, 0u32);
    llvm_asm!(
        "cpuid\n"
        : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
        : "{eax}"(0u32), "{ecx}"(0u32)
        :
        : "volatile"
    );
    b
}

extern "C" {
    fn getpid() -> i32;
}

#[inline(never)]
unsafe fn invoke(target: unsafe extern "C" fn() -> u32, seed: u64) -> (u64, u32) {
    let _ = getpid();
    let preserved: u64;
    let result: u32;
    std::arch::asm!(
        "push rbx",
        "sub rsp, 8",
        "mov rbx, r12",
        "call r13",
        "mov rdx, rbx",
        "add rsp, 8",
        "pop rbx",
        in("r12") seed,
        in("r13") target as usize,
        lateout("eax") result,
        lateout("rdx") preserved,
        clobber_abi("C"),
    );
    (preserved, result)
}

fn main() {
    let seed = 0x2468_ace0_1357_9bdfu64;
    unsafe {
        let expected = std::arch::x86_64::__cpuid_count(0, 0);
        let control = invoke(with_b_output, seed);
        assert_eq!(control, (seed, expected.ebx));
        let declared_clobber = invoke(without_b_output, seed);
        assert_eq!(declared_clobber, (seed, expected.eax ^ expected.ecx ^ expected.edx));
    }
}
