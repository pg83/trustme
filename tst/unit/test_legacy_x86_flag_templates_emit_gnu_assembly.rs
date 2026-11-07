//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only
//@ output-contains: .cpp .att_syntax prefix; pushfl; popl %%%0; .intel_syntax noprefix
//@ output-contains: .cpp .att_syntax prefix; pushl %%%0; popfl; .intel_syntax noprefix
#![feature(no_core, lang_items, rustc_attrs)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

#[rustc_builtin_macro]
macro_rules! llvm_asm { () => {} }

#[no_mangle]
pub unsafe fn read_flags() -> u32 {
    let flags: u32;
    llvm_asm!("pushfd; popl $0" : "=r"(flags) : : : "volatile");
    flags
}

#[no_mangle]
pub unsafe fn restore_flags(flags: u32) {
    llvm_asm!("pushl $0; popfd" : : "r"(flags) : : "volatile");
}
