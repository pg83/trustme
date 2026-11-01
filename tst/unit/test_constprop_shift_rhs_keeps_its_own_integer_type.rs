macro_rules! shift {
    ($left:ty, $right:ty) => {{
        const VALUE: $left = 13;
        const AMOUNT: $right = 3;
        assert_eq!(VALUE << AMOUNT, 104);
        assert_eq!(VALUE >> AMOUNT, 1);
    }};
}
macro_rules! shifts {
    ($left:ty) => {{
        shift!($left, u8); shift!($left, i8);
        shift!($left, u16); shift!($left, i16);
        shift!($left, u32); shift!($left, i32);
        shift!($left, u64); shift!($left, i64);
        shift!($left, u128); shift!($left, i128);
        shift!($left, usize); shift!($left, isize);
    }};
}

fn main() { shifts!(u8); shifts!(i16); shifts!(u128); shifts!(i128); }
