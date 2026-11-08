//@ compile-flags: -Zdump-hir
//@ output-contains: _2_hir.rs struct Marker
#![feature(rustc_attrs)]
struct Marker where u8: Copy;
#[repr(transparent)]
#[rustc_layout_scalar_valid_range_start(1)]
#[rustc_layout_scalar_valid_range_end(254)]
struct Gauge { value: u8, marker: Marker }

fn main() {
    let gauge = unsafe { Gauge { value: 7, marker: Marker } };
    assert_eq!(std::mem::size_of::<Marker>(), 0);
    assert_eq!(std::mem::size_of::<Option<Gauge>>(), 1);
    assert_eq!(gauge.value, 7);
    assert_eq!(Some(gauge).unwrap().value, 7);
    assert!(Option::<Gauge>::None.is_none());
}
