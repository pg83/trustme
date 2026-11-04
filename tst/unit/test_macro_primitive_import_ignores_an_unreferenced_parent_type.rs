macro_rules! make {
    () => {
        use u16 as Scalar;
        pub fn value() -> Scalar { 809 }
    };
}
mod scope {
    struct u16;
    mod nested { make!(); }
    pub fn value() -> core::primitive::u16 { nested::value() }
}
fn main() { assert_eq!(scope::value(), 809); }
