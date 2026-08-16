trait Identity {
    type Output;
}

struct Value(u8);

impl Value {
    fn get(self: <Value as Identity>::Output) -> u8 {
        self.0
    }

    fn get_box(self: Box<<Value as Identity>::Output>) -> u8 {
        self.0
    }
}

// Item order must not affect receiver normalization.
impl Identity for Value {
    type Output = Self;
}

fn main() {
    assert_eq!(Value(9).get(), 9);
    assert_eq!(Box::new(Value(10)).get_box(), 10);
}
