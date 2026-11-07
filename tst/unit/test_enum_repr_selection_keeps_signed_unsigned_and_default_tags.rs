#![feature(repr128)]

macro_rules! check_repr {
    ($name:ident, $repr:ident, $value:expr) => {
        #[repr($repr)]
        enum $name { Low = 0, High = $value }
        assert_eq!(std::hint::black_box($name::High) as $repr, $value);
        assert_eq!(std::mem::size_of::<$name>(), std::mem::size_of::<$repr>());
    };
}

fn main() {
    check_repr!(TagU8, u8, 31);
    check_repr!(TagU16, u16, 257);
    check_repr!(TagU32, u32, 65537);
    check_repr!(TagU64, u64, 0x1_0000_0001);
    check_repr!(TagU128, u128, 0x1_0000_0000_0000_0001);
    check_repr!(TagUsize, usize, 263);
    check_repr!(TagI8, i8, -31);
    check_repr!(TagI16, i16, -257);
    check_repr!(TagI32, i32, -65537);
    check_repr!(TagI64, i64, -0x1_0000_0001);
    check_repr!(TagI128, i128, -0x1_0000_0000_0000_0001);
    check_repr!(TagIsize, isize, -263);
    enum DefaultTag { Low, High = 17 }
    assert_eq!(std::hint::black_box(DefaultTag::High) as isize, 17);
}
