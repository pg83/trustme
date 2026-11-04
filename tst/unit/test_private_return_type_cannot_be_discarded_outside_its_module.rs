//@ compile-fail: error
macro_rules! make {
    () => {
        use u16 as Scalar;
        pub fn value() -> Scalar { 739 }
    };
}
mod scope {
    struct u16;
    mod nested { make!(); }
    pub fn read() -> u16 { u16 }
    pub fn value() -> core::primitive::u16 { nested::value() }
}
fn main() {
    let _ = scope::read();
    assert_eq!(scope::value(), 739);
}
