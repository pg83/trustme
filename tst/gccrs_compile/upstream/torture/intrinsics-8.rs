pub fn checked(value: i32, rhs: i32) -> Option<i32> {
    value.checked_add(rhs)
}

pub fn overflowing(value: i32, rhs: i32) -> [(i32, bool); 3] {
    [
        value.overflowing_add(rhs),
        value.overflowing_sub(rhs),
        value.overflowing_mul(rhs),
    ]
}
