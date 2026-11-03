#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
unsafe fn atomic_load_acquire<T: Copy>(source: *const T) -> T;
#[rustc_intrinsic]
unsafe fn atomic_load_relaxed<T: Copy>(source: *const T) -> T;
#[rustc_intrinsic]
unsafe fn atomic_load_unordered<T: Copy>(source: *const T) -> T;
#[rustc_intrinsic]
unsafe fn atomic_store_release<T: Copy>(destination: *mut T, value: T);
#[rustc_intrinsic]
unsafe fn atomic_xadd_acqrel<T: Copy>(destination: *mut T, value: T) -> T;
#[rustc_intrinsic]
unsafe fn atomic_xadd_seqcst<T: Copy>(destination: *mut T, value: T) -> T;

fn main() {
    let mut value = 7u32;
    unsafe {
        assert_eq!(atomic_load_acquire(&value), 7);
        atomic_store_release(&mut value, 11);
        assert_eq!(atomic_load_relaxed(&value), 11);
        assert_eq!(atomic_xadd_acqrel(&mut value, 5), 11);
        assert_eq!(atomic_xadd_seqcst(&mut value, 3), 16);
        assert_eq!(atomic_load_unordered(&value), 19);
    }
    assert_eq!(value, 19);
}
