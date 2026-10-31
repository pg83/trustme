fn add(value: u16) -> u16 { value + 7 }
fn main() {
    let safe: fn(u16) -> u16 = add;
    let pointer = safe as unsafe fn(u16) -> u16;
    unsafe { assert_eq!(pointer(30), 37); }
}
