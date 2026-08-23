#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

unsafe extern "C" {
    fn trustme_order_external(value: u32) -> u32;
}

#[inline(never)]
fn trustme_order_leaf(value: u32) -> u32 {
    value + 1
}

#[inline(never)]
fn trustme_order_middle(value: u32) -> u32 {
    trustme_order_leaf(value)
}

#[inline(never)]
fn trustme_recursive_a(value: u32) -> u32 {
    if value == 0 {
        0
    } else {
        trustme_recursive_b(value - 1)
    }
}

#[inline(never)]
fn trustme_recursive_b(value: u32) -> u32 {
    if value == 0 {
        0
    } else {
        trustme_recursive_a(value - 1)
    }
}

#[no_mangle]
fn trustme_order_root(value: u32) -> u32 {
    unsafe {
        trustme_order_middle(value)
            + trustme_recursive_a(value)
            + trustme_order_external(value)
    }
}
