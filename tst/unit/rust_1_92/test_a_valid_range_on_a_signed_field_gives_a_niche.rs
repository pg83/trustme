// std 1.92's `OwnedFd` and `BorrowedFd` hold `core::num::niche_types::
// I32NotAllOnes`, an `i32` with `#[rustc_layout_scalar_valid_range_start(0)]`
// and `#[rustc_layout_scalar_valid_range_end(0xffff_fffe)]`. rustc applies
// both bounds to the field's scalar, read as unsigned bits whatever the
// integer's sign, so `-1` is the niche and `Option<OwnedFd>` is 4 bytes. We
// kept only a start of 1 and an end on an unsigned field, so the signed
// field had no niche and the option took 8. io-lifetimes'
// `test_niche_optimizations`.
use std::mem::size_of;
use std::os::fd::{BorrowedFd, FromRawFd, IntoRawFd, OwnedFd, RawFd};

fn main() {
    println!("{} {} {}", size_of::<Option<OwnedFd>>(), size_of::<Option<BorrowedFd<'static>>>(), size_of::<RawFd>());
    assert_eq!(size_of::<Option<OwnedFd>>(), size_of::<RawFd>());
    assert_eq!(size_of::<Option<BorrowedFd<'static>>>(), size_of::<RawFd>());
    unsafe {
        assert_eq!(Some(OwnedFd::from_raw_fd(RawFd::MIN)).unwrap().into_raw_fd(), RawFd::MIN);
        assert_eq!(Some(OwnedFd::from_raw_fd(RawFd::MAX)).unwrap().into_raw_fd(), RawFd::MAX);
    }
    let none: Option<OwnedFd> = None;
    assert!(none.is_none());
}
