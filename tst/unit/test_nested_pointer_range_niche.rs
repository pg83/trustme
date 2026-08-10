#![feature(rustc_attrs)]

use std::mem;

#[rustc_layout_scalar_valid_range_start(1)]
#[rustc_layout_scalar_valid_range_end(100)]
#[derive(Copy, Clone)]
struct PointerWithRange(*const u8);

fn main() {
    let value = unsafe { PointerWithRange(std::ptr::without_provenance(90)) };
    let some = Some(Some(value));
    assert!(some.unwrap().is_some());
    assert_eq!(unsafe { mem::transmute::<_, usize>(some) }, 90);

    let none: Option<Option<PointerWithRange>> = None;
    let raw = unsafe { mem::transmute::<_, usize>(none) };
    assert!(!(1..=100).contains(&raw));
}
