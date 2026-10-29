mod u16 {
    pub struct Token(pub u16);
    pub const LIMIT: u16 = 17;
    pub mod nested {
        pub fn value() -> u16 { 23 }
    }
}
fn main() {
    let token: u16::Token = u16::Token(u16::LIMIT);
    assert_eq!(token.0, 17);
    assert_eq!(u16::nested::value(), 23);
    assert_eq!(u16::MAX, 65535);
    assert_eq!(u16::from_be_bytes([0, 29]), 29);
}
