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

#[rustc_builtin_macro]
macro_rules! asm { () => {} }

#[no_mangle]
pub unsafe extern "C" fn add_eleven(value: u64) -> u64 {
    let result: u64;
    asm!("addi {result}, {value}, 11",
         value = in(reg) value, result = lateout(reg) result, options(nomem, nostack));
    result
}
