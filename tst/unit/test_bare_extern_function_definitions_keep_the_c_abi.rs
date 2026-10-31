extern fn combine(a: u32, b: u32) -> u32 { a * 10 + b }
fn main() {
    let pointer: extern "C" fn(u32, u32) -> u32 = combine;
    assert_eq!(pointer(3, 7), 37);
}
