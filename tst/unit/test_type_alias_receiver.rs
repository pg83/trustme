struct Value(u8);

type Alias = Value;

impl Value {
    fn get(self: &Alias) -> u8 {
        self.0
    }
}

fn main() {
    assert_eq!(Value(7).get(), 7);
}
