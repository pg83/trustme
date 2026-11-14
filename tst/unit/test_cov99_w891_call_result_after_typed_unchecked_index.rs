//@ compile-flags: -Z mir-opt-level=1 -Z dump-mir -C panic=abort
#![feature(core_intrinsics)]

#[inline(never)]
fn produce(seed: u32) -> u32 {
    seed ^ 0x5a
}

#[inline(never)]
fn store(values: &mut [u32], index: usize, seed: u32) {
    unsafe {
        *core::intrinsics::slice_get_unchecked::<&mut u32, _, _>(values, index & 3) = produce(seed);
    }
}

fn main() {
    let mut values = [10, 20, 30, 40];
    store(&mut values, 5, 9);
    assert_eq!(values, [10, 83, 30, 40]);
}
