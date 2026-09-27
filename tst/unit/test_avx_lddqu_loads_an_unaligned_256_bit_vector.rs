// llvm.x86.avx.ldu.dq.256 (_mm256_lddqu_si256) had no lowering and aborted
// on `assert(!"Extern LLVM: ...")`; httparse's AVX2 URI scanner uses it.
// VLDDQU loads 32 bytes from any address, aligned or not (Intel SDM).
#[cfg(target_arch = "x86_64")]
use core::arch::x86_64::*;

#[cfg(target_arch = "x86_64")]
#[target_feature(enable = "avx")]
unsafe fn check() -> bool {
    let mut bytes = [0u8; 40];
    for (i, b) in bytes.iter_mut().enumerate() {
        *b = i as u8;
    }
    let v = _mm256_lddqu_si256(bytes.as_ptr().add(3) as *const __m256i);
    let r: [u8; 32] = core::mem::transmute(v);
    r.iter().enumerate().all(|(i, &b)| b == i as u8 + 3)
}

fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx") {
        assert!(unsafe { check() });
        return;
    }
    println!("skip");
}
