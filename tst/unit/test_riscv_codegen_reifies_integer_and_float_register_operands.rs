//@ crate-type: lib
//@ compile-only
//@ compile-flags: --target=riscv64-unknown-linux-gnu --emit=llvm-ir
#![no_core]
#![feature(no_core, lang_items, rustc_attrs)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy: Sized {}
impl Copy for u64 {}
impl Copy for f32 {}

#[rustc_builtin_macro]
macro_rules! asm { () => {} }

#[no_mangle]
pub unsafe extern "C" fn add_nine(mut value: u64) -> u64 {
    asm!("addi {value}, {value}, 9", value = inout(reg) value, options(nomem, nostack));
    value
}

#[no_mangle]
pub unsafe extern "C" fn copy_float(value: f32) -> f32 {
    let result: f32;
    asm!("fsgnj.s {result}, {value}, {value}",
         value = in(freg) value, result = lateout(freg) result, options(nomem, nostack));
    result
}
