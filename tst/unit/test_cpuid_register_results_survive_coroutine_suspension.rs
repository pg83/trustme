#![feature(coroutines, coroutine_trait, stmt_expr_attributes, rustc_attrs)]
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;
#[rustc_builtin_macro]
macro_rules! llvm_asm { () => {} }
fn main() {
    for (leaf, subleaf) in [(0u32, u32::MAX), (0x80000000, 0), (7, 1)] {
        let expected = unsafe { std::arch::x86_64::__cpuid_count(leaf, subleaf) };
        let expected = [expected.eax, expected.ebx, expected.ecx, expected.edx];
        for dialect in 0..3 {
            let mut task = #[coroutine] move || {
                let (a, b, c, d): (u32, u32, u32, u32);
                match dialect {
                    0 => unsafe {
                        llvm_asm!("cpuid" : "={eax}"(a), "={ebx}"(b), "={ecx}"(c), "={edx}"(d)
                            : "{eax}"(leaf), "{ecx}"(subleaf) : "rbx" : "volatile");
                    },
                    1 => unsafe {
                        let saved: u64;
                        std::arch::asm!("movq %rbx, {0:r}", "cpuid", "xchgq %rbx, {0:r}",
                            lateout(reg) saved, inlateout("eax") leaf => a,
                            inlateout("ecx") subleaf => c, lateout("edx") d,
                            options(att_syntax, nostack));
                        b = saved as u32;
                    },
                    _ => unsafe {
                        let saved: u64;
                        std::arch::asm!("mov {0:r}, rbx", "cpuid", "xchg {0:r}, rbx",
                            out(reg) saved, inout("eax") leaf => a,
                            inout("ecx") subleaf => c, out("edx") d, options(nostack));
                        b = saved as u32;
                    },
                }
                yield (a ^ b ^ c ^ d) as u64;
                [a, b, c, d]
            };
            assert_eq!(Pin::new(&mut task).resume(()), CoroutineState::Yielded((expected[0] ^ expected[1] ^ expected[2] ^ expected[3]) as u64));
            assert_eq!(Pin::new(&mut task).resume(()), CoroutineState::Complete(expected));
        }
    }
}
