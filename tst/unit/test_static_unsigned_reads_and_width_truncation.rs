//@ compile-flags: -Coverflow-checks=off
static COUNT: usize = 239;
static REAL: f32 = 3.5;
fn widths() -> (u8, i8, isize, usize, i64, u32) {
    let unsigned = 259usize;
    let signed = -37i64;
    let real = REAL;
    (unsigned as u8, signed as i8, signed as isize, unsigned + 2, signed + 2, real as u32)
}
fn in_range() -> bool { COUNT <= 239 && COUNT > 230 }
fn main() {
    assert_eq!(widths(), (3, -37, -37, 261, -35, 3));
    assert!(in_range());
}
