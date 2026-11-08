//@ compile-flags: -O

#[inline(never)]
fn select(mut current: *const u64, alternate: *const u64) -> [*const u64; 2] {
    current = unsafe { &*current } as *const u64;
    let first = current;
    current = alternate;
    current = unsafe { &*current } as *const u64;
    [first, current]
}

fn main() {
    let first = 37u64;
    let second = 61u64;
    let addresses = select(&first, &second);
    assert_eq!(addresses[0], &first as *const u64);
    assert_eq!(addresses[1], &second as *const u64);
    assert_eq!(unsafe { *addresses[0] + *addresses[1] }, 98);
}
