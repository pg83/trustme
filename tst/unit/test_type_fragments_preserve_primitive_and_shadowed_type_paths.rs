macro_rules! spelling { ($ty:ty) => { stringify!($ty) }; }

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

mod shadowed {
    #[allow(non_camel_case_types)]
    pub type u16 = u8;
    pub fn width() -> usize { std::mem::size_of::<u16>() }
}

fn generic<T>() -> (usize, &'static str) {
    (std::mem::size_of::<T>(), spelling!(T))
}

fn main() {
    assert_eq!(spelling!(u16), "u16");
    assert_eq!(compact(spelling!(::core::primitive::u16)), "::core::primitive::u16");
    assert_eq!(compact(spelling!(shadowed::u16)), "shadowed::u16");
    assert_eq!(compact(spelling!(fn(u8) -> ())), "fn(u8)->()");
    assert_eq!(generic::<u32>(), (4, "T"));
    assert_eq!(shadowed::width(), 1);
    assert_eq!(std::mem::size_of::<u16>(), 2);
}
