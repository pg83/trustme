pub fn swap_and_return_old(destination: &mut i32, mut source: i32) -> i32 {
    std::mem::swap(destination, &mut source);
    source
}

pub fn overflow_and_rotate(value: u32, rhs: u32) -> ((u32, bool), u32, u32) {
    (
        value.overflowing_add(rhs),
        value.rotate_left(rhs),
        value.rotate_right(rhs),
    )
}
