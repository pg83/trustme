#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
unsafe fn atomic_cxchg_release<T: Copy>(destination: *mut T, expected: T, replacement: T) -> (T, bool);

#[rustc_intrinsic]
unsafe fn atomic_cxchg_acqrel_relaxed<T: Copy>(destination: *mut T, expected: T, replacement: T) -> (T, bool);

fn main() {
    let mut released = 7u32;
    unsafe {
        assert_eq!(atomic_cxchg_release(&mut released, 7, 19), (7, true));
        assert_eq!(atomic_cxchg_release(&mut released, 7, 23), (19, false));
    }
    assert_eq!(released, 19);

    let mut acquired = -29i64;
    unsafe {
        assert_eq!(atomic_cxchg_acqrel_relaxed(&mut acquired, -29, 41), (-29, true));
        assert_eq!(atomic_cxchg_acqrel_relaxed(&mut acquired, -29, 43), (41, false));
    }
    assert_eq!(acquired, 41);
}
