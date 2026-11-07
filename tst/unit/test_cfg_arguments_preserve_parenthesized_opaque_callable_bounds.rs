fn callback(
    #[cfg(any())] omitted_first: u32,
    base: u16,
    tail: u8,
    #[cfg_attr(all(), cfg(any()))] omitted_last: u64,
) -> impl (Fn(u8) -> u16) + Copy {
    move |extra| base + u16::from(tail) + u16::from(extra)
}

fn main() {
    let function = callback(149, 7);
    let duplicate = function;
    assert_eq!(function(11), 167);
    assert_eq!(duplicate(13), 169);
}
