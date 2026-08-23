#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

unsafe extern "C" {
    fn trustme_cfg_leaf(value: u32) -> u32;
}

#[no_mangle]
fn trustme_call_then_return(value: u32) -> u32 {
    unsafe { trustme_cfg_leaf(value) }
}

#[inline(never)]
fn trustme_noop_drop_chain<T>(value: T) -> u32 {
    let result = unsafe { trustme_cfg_leaf(33) };
    let _ = &value;
    result
}

#[no_mangle]
fn trustme_instantiate_noop_drop(value: u32) -> u32 {
    trustme_noop_drop_chain(value)
}

#[no_mangle]
fn trustme_branch_fallthrough(condition: bool) -> u32 {
    if condition { 11 } else { 22 }
}
