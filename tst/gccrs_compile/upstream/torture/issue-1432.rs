trait UintExt: Sized {
    fn wrapping_add_ext(self, rhs: Self) -> Self;
    fn rotate_left_ext(self, amount: u32) -> Self;
    fn rotate_right_ext(self, amount: u32) -> Self;
}

macro_rules! impl_uint {
    ($($ty:ty),* $(,)?) => {
        $(
            impl UintExt for $ty {
                fn wrapping_add_ext(self, rhs: Self) -> Self {
                    self.wrapping_add(rhs)
                }

                fn rotate_left_ext(self, amount: u32) -> Self {
                    self.rotate_left(amount)
                }

                fn rotate_right_ext(self, amount: u32) -> Self {
                    self.rotate_right(amount)
                }
            }
        )*
    };
}

impl_uint!(u8, u16, u32, u64, usize);

pub fn exercise(value: u32, rhs: u32) -> (u32, u32, u32) {
    (
        value.wrapping_add_ext(rhs),
        value.rotate_left_ext(rhs),
        value.rotate_right_ext(rhs),
    )
}
