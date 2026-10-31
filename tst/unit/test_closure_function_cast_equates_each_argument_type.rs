fn main() {
    let combine = (|a: u16, b: u32| a as u32 + b) as fn(u16, u32) -> u32;
    assert_eq!(combine(7, 30), 37);
}
