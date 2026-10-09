pub trait ConstZero {
    const ZERO: Self;
}

impl ConstZero for f32 {
    const ZERO: f32 = 0.0;
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Complex<T> {
    pub re: T,
    pub im: T,
}

impl<T> Complex<T> {
    pub const fn new(re: T, im: T) -> Self {
        Complex { re, im }
    }
}

impl<T: ConstZero> Complex<T> {
    pub const ZERO: Self = Self::new(T::ZERO, T::ZERO);
}
