pub fn identity<T>(x: T) -> T {
    x
}

pub fn upstream_pointer() -> fn(u32) -> u32 {
    identity::<u32>
}
