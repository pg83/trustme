use std::ops::Mul;

trait CastInto<T: Copy>: Copy {
    fn cast(self) -> T;
}

trait CastFrom<T: Copy>: Copy {}

impl<T: Copy, U: CastInto<T> + Copy> CastFrom<U> for T {}

impl CastInto<u32> for i32 {
    fn cast(self) -> u32 {
        self as u32
    }
}

impl CastInto<u16> for i32 {
    fn cast(self) -> u16 {
        self as u16
    }
}

impl CastInto<u16> for u128 {
    fn cast(self) -> u16 {
        self as u16
    }
}

trait Int: Copy + Mul<Output = Self> + PartialOrd + CastFrom<i32> {}

impl Int for u16 {}
impl Int for u32 {}

trait DoubleWidth: Int {
    type Half: Int;
}

impl DoubleWidth for u32 {
    type Half = u16;
}

trait Float {
    type Int: Int + DoubleWidth;
}

struct F32;

impl Float for F32 {
    type Int = u32;
}

fn round<F: Float>(residual: F::Int, significand: F::Int) -> bool
where
    i32: CastInto<F::Int>,
    u128: CastInto<<F::Int as DoubleWidth>::Half>,
{
    residual > (2 + 1).cast() * significand
}

fn main() {
    assert!(round::<F32>(10, 3));
}
