//@ run-pass
// portable-atomic loads and stores an `AtomicU128` with `vmovdqa` through an
// `xmm_reg` operand of type `__m128i` once AVX is detected. rustc hands any
// template to the assembler; a template starting with `vmov` was replaced by
// `abort()`, so `static_load_only` aborted.
#[cfg(target_arch = "x86_64")]
mod vmovdqa {
    use std::arch::asm;
    use std::arch::x86_64::__m128i;

    #[target_feature(enable = "avx")]
    pub unsafe fn load(src: *const u128) -> u128 {
        let out: __m128i;
        unsafe {
            asm!(
                "vmovdqa {out}, xmmword ptr [{src}]",
                src = in(reg) src,
                out = out(xmm_reg) out,
                options(nostack, preserves_flags),
            );
            core::mem::transmute::<__m128i, u128>(out)
        }
    }

    #[target_feature(enable = "avx")]
    pub unsafe fn store(dst: *mut u128, val: u128) {
        unsafe {
            let val: __m128i = core::mem::transmute(val);
            asm!(
                "vmovdqa xmmword ptr [{dst}], {val}",
                dst = in(reg) dst,
                val = in(xmm_reg) val,
                options(nostack, preserves_flags),
            );
        }
    }
}

#[repr(align(16))]
struct Aligned(u128);

fn main() {
    #[cfg(target_arch = "x86_64")]
    if std::is_x86_feature_detected!("avx") {
        let mut cell = Aligned(0x0123_4567_89ab_cdef_fedc_ba98_7654_3210);
        unsafe {
            assert_eq!(vmovdqa::load(&cell.0), 0x0123_4567_89ab_cdef_fedc_ba98_7654_3210);
            vmovdqa::store(&mut cell.0, 42);
        }
        assert_eq!(cell.0, 42);
    }
}
