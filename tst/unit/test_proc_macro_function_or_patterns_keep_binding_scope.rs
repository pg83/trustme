//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
enum Token { Byte(u8), Escaped(u8) }
macro_rules! relay {
    ($item:item) => { proc_macro_item_passthrough::echo! { $item } };
}
relay! {
    fn byte((Token::Byte(value) | Token::Escaped(value)): Token) -> u8 { value }
}
relay! {
    fn borrowed(&(Token::Byte(value) | Token::Escaped(value)): &Token) -> u8 { value + 1 }
}
fn main() {
    assert_eq!(byte(Token::Byte(11)), 11);
    assert_eq!(byte(Token::Escaped(19)), 19);
    assert_eq!(borrowed(&Token::Byte(23)), 24);
    assert_eq!(borrowed(&Token::Escaped(29)), 30);
}
