//@ crate-type: lib
//@ compile-only
//@ edition: 2024
//@ compile-flags: --target=x86_64-unknown-linux-gnu --emit=link -Copt-level=0 -Cpanic=abort
#![feature(no_core, lang_items, link_llvm_intrinsics, repr_simd, simd_ffi, abi_unadjusted)]
#![no_core]
#![allow(internal_features, improper_ctypes)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}
impl Copy for *mut u8 {}
impl Copy for *const u8 {}
impl Copy for u32 {}
impl Copy for u64 {}

#[repr(simd)]
pub struct ByteMask([i8; 16]);

unsafe extern "C" {
    #[link_name = "llvm.x86.sse.stmxcsr"]
    fn store_control(output: *mut u8);
    #[link_name = "llvm.x86.sse.ldmxcsr"]
    fn load_control(input: *const u8);
    #[link_name = "llvm.x86.sse2.clflush"]
    fn flush(input: *const u8);
    #[link_name = "llvm.x86.sse2.maskmov.dqu"]
    fn masked_store(data: ByteMask, mask: ByteMask, output: *mut u8);
    #[link_name = "llvm.x86.fxsave"]
    fn save_legacy(output: *mut u8);
    #[link_name = "llvm.x86.fxrstor"]
    fn restore_legacy(input: *const u8);
    #[link_name = "llvm.x86.xsave"]
    fn save_extended(output: *mut u8, high: u32, low: u32);
    #[link_name = "llvm.x86.xsaveopt"]
    fn save_optimized(output: *mut u8, high: u32, low: u32);
    #[link_name = "llvm.x86.xsavec"]
    fn save_compacted(output: *mut u8, high: u32, low: u32);
    #[link_name = "llvm.x86.xsaves"]
    fn save_supervisor(output: *mut u8, high: u32, low: u32);
    #[link_name = "llvm.x86.xrstor"]
    fn restore_extended(input: *const u8, high: u32, low: u32);
    #[link_name = "llvm.x86.xrstors"]
    fn restore_supervisor(input: *const u8, high: u32, low: u32);
    #[link_name = "llvm.x86.xsetbv"]
    fn set_extended_control(register: u32, high: u32, low: u32);
    #[link_name = "llvm.x86.rdtsc"]
    fn read_timestamp() -> u64;
}

unsafe extern "unadjusted" {
    #[link_name = "llvm.x86.rdtscp"]
    fn read_timestamp_aux() -> (u64, u32);
}

#[unsafe(no_mangle)]
#[target_feature(enable = "sse2,fxsr,xsave,xsaveopt,xsavec,xsaves")]
pub unsafe fn cov99_native_state_intrinsics(
    data: ByteMask,
    mask: ByteMask,
    output: *mut u8,
    input: *const u8,
    register: u32,
    high: u32,
    low: u32,
) -> (u64, (u64, u32)) {
    unsafe {
        store_control(output);
        load_control(input);
        flush(input);
        masked_store(data, mask, output);
        save_legacy(output);
        restore_legacy(input);
        save_extended(output, high, low);
        save_optimized(output, high, low);
        save_compacted(output, high, low);
        save_supervisor(output, high, low);
        restore_extended(input, high, low);
        restore_supervisor(input, high, low);
        set_extended_control(register, high, low);
        (read_timestamp(), read_timestamp_aux())
    }
}
