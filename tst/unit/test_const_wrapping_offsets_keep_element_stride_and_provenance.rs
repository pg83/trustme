static WORDS: [u32; 5] = [11, 17, 23, 31, 43];
const fn selected() -> (u32, u32) {
    let first = WORDS.as_ptr();
    let last = first.wrapping_offset(4);
    let second = last.wrapping_offset(-3);
    unsafe { (*last, *second) }
}
const VALUES: (u32, u32) = selected();
fn main() { assert_eq!(VALUES, (43, 17)); }
