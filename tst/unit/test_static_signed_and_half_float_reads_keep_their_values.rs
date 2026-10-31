#![feature(f16)]
static SIGNED: isize = -37;
static HALF: f16 = 1.5f16;
fn main() {
    assert_eq!(SIGNED + 43, 6);
    assert_eq!(HALF as f32, 1.5f32);
}
