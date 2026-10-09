// `as` from a float to an integer saturates (since Rust 1.45): NaN is 0, a
// value past either end is that end, anything else truncates toward zero.
// rustc lowers it to `llvm.fpto{s,u}i.sat`. We emitted a plain C cast, which
// is undefined out of range and in practice wrapped: image's PNM decoder
// scales a 0/255 bitmap by 255 with `(v * 255.0).round() as u8` and got 1.
use std::hint::black_box;

fn main() {
    assert_eq!((black_box(255f32) * black_box(255f32)).round() as u8, 255);
    assert_eq!(black_box(-1.5f32) as u8, 0);
    assert_eq!(black_box(-0.5f32) as u8, 0);
    assert_eq!(black_box(f32::NAN) as i32, 0);
    assert_eq!(black_box(f64::NAN) as u64, 0);
    assert_eq!(black_box(1e10f64) as i32, i32::MAX);
    assert_eq!(black_box(-1e10f64) as i32, i32::MIN);
    assert_eq!(black_box(300.7f64) as i8, 127);
    assert_eq!(black_box(-300.7f64) as i8, -128);
    assert_eq!(black_box(-128.9f32) as i8, -128);
    assert_eq!(black_box(1e30f32) as u64, u64::MAX);
    assert_eq!(black_box(1e30f64) as i64, i64::MAX);
    assert_eq!(black_box(-1e30f64) as i64, i64::MIN);
    assert_eq!(black_box(4294967296.0f64) as u32, u32::MAX);
    assert_eq!(black_box(4294967295.9f64) as u32, u32::MAX);
    assert_eq!(black_box(f64::INFINITY) as u16, u16::MAX);
    assert_eq!(black_box(f64::NEG_INFINITY) as i16, i16::MIN);
    assert_eq!(black_box(42.9f64) as usize, 42);
    assert_eq!(black_box(-42.9f64) as isize, -42);
}
