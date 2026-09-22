// llvm.x86.avx2.vperm2i128 (_mm256_permute2x128_si256) had no lowering and
// aborted on `assert(!"Extern LLVM: ...")`; aho-corasick's AVX2 Teddy uses
// it. Each 128-bit half of the result is one of the four halves of `a` and
// `b`, chosen by imm8[1:0] (low half) and imm8[5:4] (high half); imm8 bit 3
// and bit 7 zero the half instead (Intel SDM, VPERM2I128).
#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx2")]
unsafe fn check() -> bool {
    let a = _mm256_setr_epi64x(1, 2, 3, 4);
    let b = _mm256_setr_epi64x(5, 6, 7, 8);
    let r: [i64; 4] = core::mem::transmute(_mm256_permute2x128_si256::<0x21>(a, b));
    if r != [3, 4, 5, 6] {
        return false;
    }
    let r: [i64; 4] = core::mem::transmute(_mm256_permute2x128_si256::<0x03>(a, b));
    if r != [7, 8, 1, 2] {
        return false;
    }
    let r: [i64; 4] = core::mem::transmute(_mm256_permute2x128_si256::<0x82>(a, b));
    if r != [5, 6, 0, 0] {
        return false;
    }
    let r: [i64; 4] = core::mem::transmute(_mm256_permute2x128_si256::<0x1b>(a, b));
    if r != [0, 0, 3, 4] {
        return false;
    }
    true
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx2") {
        assert!(unsafe { check() });
        return;
    }
    println!("skip");
}
