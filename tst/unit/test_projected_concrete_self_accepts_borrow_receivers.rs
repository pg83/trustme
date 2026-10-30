trait Family {
    type Item;
}

struct Value(u8);

impl Family for Value {
    type Item = Value;
}

impl Value {
    fn peek_value(self: &<Self as Family>::Item) -> u8 {
        self.0
    }

    fn set_value(self: &mut <Self as Family>::Item, value: u8) {
        self.0 = value;
    }
}

fn main() {
    let mut value = Value(7);
    assert_eq!(value.peek_value(), 7);
    value.set_value(13);
    assert_eq!(value.peek_value(), 13);
}
