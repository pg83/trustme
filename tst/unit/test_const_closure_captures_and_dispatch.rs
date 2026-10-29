#![feature(const_closures, const_trait_impl)]
struct Token(u8);
const fn consume(token: Token) -> u8 { token.0 }
const SHARED: u8 = {
    let value = 4u8;
    (const || value + 2)()
};
const MUTABLE: u8 = {
    let mut value = 2u8;
    let mut bump = const || { value += 3; value };
    bump() + bump()
};
const ONCE: u8 = {
    let token = Token(9);
    (const move || consume(token))()
};
const NESTED: u8 = (const || (const |value: u8| value + 1)(5))();
fn main() {
    assert_eq!((SHARED, MUTABLE, ONCE, NESTED), (6, 13, 9, 6));
}
