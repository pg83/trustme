static LEFT: u32 = 17;
static RIGHT: u32 = 29;

const SEED: [*const u32; 2] = [&LEFT, &RIGHT];

#[derive(Clone, Copy)]
union Slot {
    pointer: *const u32,
    bits: usize,
}

const fn rebuild() -> ([*const u32; 2], usize) {
    let mut pointers = SEED;
    pointers[0] = &RIGHT;
    pointers[1] = &LEFT;
    let copied = pointers;
    let mut slot = Slot { pointer: copied[1] };
    slot.pointer = copied[0];
    let saved = slot;
    slot.bits = 0;
    unsafe { ([saved.pointer, slot.pointer], slot.bits) }
}

const RESULT: ([*const u32; 2], usize) = rebuild();

fn main() {
    assert_eq!(RESULT.0[0], &RIGHT as *const u32);
    assert!(RESULT.0[1].is_null());
    assert_eq!(RESULT.1, 0);
    assert_eq!(unsafe { *RESULT.0[0] }, 29);
}
