// An array size cast from an enum variant (tiny-skia's STAGES_COUNT) was folded
// with still-zero discriminants when the enum was visited later.
#[allow(dead_code)]
#[derive(Copy, Clone)]
pub enum Stage { A = 0, B, C }
pub const COUNT: usize = Stage::C as usize + 1;
static TABLE: [u8; COUNT] = [10, 20, 30];
fn main() {
    assert_eq!(Stage::C as usize, 2);
    assert_eq!(COUNT, 3);
    assert_eq!(TABLE[2], 30);
}
