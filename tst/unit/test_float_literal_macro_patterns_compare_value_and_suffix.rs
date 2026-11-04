macro_rules! classify {
    (1.25f32) => { 101u32 };
    (1.25f64) => { 103u32 };
    (2.5) => { 107u32 };
    ($other:literal) => { 109u32 };
}
fn main() {
    assert_eq!(classify!(1.25f32), 101);
    assert_eq!(classify!(1.25f64), 103);
    assert_eq!(classify!(2.5), 107);
    assert_eq!(classify!(9.75), 109);
}
