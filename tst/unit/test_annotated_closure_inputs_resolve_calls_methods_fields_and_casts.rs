struct Value { byte: u8 }
impl Value { fn byte(&self) -> u8 { self.byte + 1 } }
fn byte() -> u8 { 11 }

fn main() {
    let call = |callable: fn() -> u8| -> u8 { callable() };
    let method = |receiver: Value| -> u8 { receiver.byte() };
    let field = |receiver: Value| -> u8 { receiver.byte };
    let cast = |value: u16| { value as u8 };
    assert_eq!(call(byte), 11);
    assert_eq!(method(Value { byte: 17 }), 18);
    assert_eq!(field(Value { byte: 23 }), 23);
    assert_eq!(cast(259), 3);
}
