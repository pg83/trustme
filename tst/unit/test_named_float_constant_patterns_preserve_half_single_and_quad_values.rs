#![feature(f16, f128)]
macro_rules! check {
    ($name:ident, $ty:ty) => {
        fn $name(value: $ty) -> bool {
            const PATTERN: $ty = 1.5;
            match value { PATTERN => true, _ => false }
        }
    };
}
check!(half, f16);
check!(single, f32);
check!(quad, f128);
fn main() {
    assert!(half(1.5) && !half(2.0));
    assert!(single(1.5) && !single(2.0));
    assert!(quad(1.5) && !quad(2.0));
}
