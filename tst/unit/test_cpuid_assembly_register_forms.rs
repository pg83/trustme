#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! llvm_asm { () => {} }

#[inline(never)]
fn legacy(leaf: u32, subleaf: u32) -> [u32; 4] {
    let (a, b, c, d): (u32, u32, u32, u32);
    unsafe {
        llvm_asm!("cpuid"
            : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
            : "{eax}"(leaf), "{ecx}"(subleaf)
            : "rbx" : "volatile");
    }
    [a, b, c, d]
}

#[inline(never)]
fn att(leaf: u32, subleaf: u32) -> [u32; 4] {
    let (a, b, c, d): (u32, u64, u32, u32);
    unsafe {
        std::arch::asm!("movq %rbx, {0:r}", "cpuid", "xchgq %rbx, {0:r}",
            lateout(reg) b, inlateout("eax") leaf => a,
            inlateout("ecx") subleaf => c, lateout("edx") d,
            options(att_syntax, nostack));
    }
    [a, b as u32, c, d]
}

#[inline(never)]
fn intel(leaf: u32, subleaf: u32) -> [u32; 4] {
    let (a, b, c, d): (u32, u64, u32, u32);
    unsafe {
        std::arch::asm!("mov {0:r}, rbx", "cpuid", "xchg {0:r}, rbx",
            out(reg) b, inout("eax") leaf => a,
            inout("ecx") subleaf => c, out("edx") d,
            options(nostack));
    }
    [a, b as u32, c, d]
}

fn main() {
    for leaf in [0, 7] {
        let leaf = std::hint::black_box(leaf);
        let expected = unsafe { std::arch::x86_64::__cpuid_count(leaf, 0) };
        let expected = [expected.eax, expected.ebx, expected.ecx, expected.edx];
        assert_eq!(legacy(leaf, 0), expected);
        assert_eq!(att(leaf, 0), expected);
        assert_eq!(intel(leaf, 0), expected);
    }
}
