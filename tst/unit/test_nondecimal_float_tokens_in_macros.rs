macro_rules! ignore {
    ($token:literal) => { 17 };
}
macro_rules! display {
    ($token:literal) => { stringify!($token) };
}

fn main() {
    assert_eq!(ignore!(0b101f32), 17);
    assert_eq!(ignore!(0o17f64), 17);
    assert_eq!(display!(0b101f32), "0b101f32");
    assert_eq!(display!(0o17f64), "0o17f64");
    assert_eq!(stringify!(0b101f16 0o17f128), "0b101f16 0o17f128");
    let decimal: f32 = 101f32;
    let hexadecimal: u32 = 0x1_f32;
    assert_eq!(decimal, 101.0);
    assert_eq!(hexadecimal, 7986);
}
