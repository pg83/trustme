fn main() {
    mod local { pub struct Token(pub u16); }
    use local::Token;
    let Token(value) = Token(37);
    assert_eq!(value, 37);
}
