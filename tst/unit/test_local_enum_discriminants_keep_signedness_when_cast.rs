#[repr(i8)]
enum Signed { Low = -117, High = 37 }
#[repr(u16)]
enum Unsigned { Low = 37, High = 60000 }
fn signed() -> (i16, u8) {
    let low = Signed::Low;
    let high = Signed::High;
    (low as i16, high as u8)
}
fn unsigned() -> (i32, u8) {
    let low = Unsigned::Low;
    let high = Unsigned::High;
    (low as i32, high as u8)
}
fn main() {
    assert_eq!(signed(), (-117, 37));
    assert_eq!(unsigned(), (37, 96));
    assert_eq!((Signed::High as i16) as f64, 37.0);
    assert_eq!((Signed::High as u8) as char, '%');
}
