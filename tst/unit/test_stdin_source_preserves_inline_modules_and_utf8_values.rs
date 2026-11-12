//@ stdin-source

mod supplied {
    pub struct Value(pub u16);

    impl Value {
        pub fn plus_two(&self) -> u16 {
            self.0 + 2
        }
    }

    pub fn label() -> &'static str {
        "поток"
    }
}

fn main() {
    let value = supplied::Value(853);
    assert_eq!(value.plus_two(), 855);
    assert_eq!(supplied::label(), "поток");
}
