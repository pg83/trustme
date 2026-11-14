#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]

const DANGLING: &() = unsafe { &*(0x1000usize as *const ()) };
const LEFT: [&(); 2] = [DANGLING, &()];
const RIGHT: [&(); 2] = [&(), DANGLING];

#[inline(never)]
fn marker<const KEY: [&'static (); 2]>() -> usize {
    std::hint::black_box(7)
}

fn main() {
    assert_eq!(marker::<LEFT>() + marker::<RIGHT>(), 14);
}
