//@ compile-variant: -O
#![feature(core_intrinsics)]

#[inline(never)]
fn same(flag: bool, a: *const [u32]) -> usize {
    if flag { core::intrinsics::ptr_metadata(a) } else { core::intrinsics::ptr_metadata(a) }
}

#[inline(never)]
fn different(flag: bool, a: *const [u32], b: *const [u32]) -> usize {
    if flag { core::intrinsics::ptr_metadata(a) } else { core::intrinsics::ptr_metadata(b) }
}

fn main() {
    let data = [17u32, 31, 47, 61, 79, 97, 113];
    let a = &data[..3] as *const [u32];
    let b = &data[3..] as *const [u32];
    for flag in [false, true] {
        assert_eq!(same(flag, a), 3);
        assert_eq!(different(flag, a, b), if flag { 3 } else { 4 });
    }
}
