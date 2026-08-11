pub fn assume_twelve(value: i32) -> i32 {
    if value != 12 {
        unsafe { std::hint::unreachable_unchecked() }
    }
    value
}
