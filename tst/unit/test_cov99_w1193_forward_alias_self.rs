mod payload {
    pub struct Token(pub u8);
}

impl LaterAlias {
    fn make(value: u8) -> Self {
        Self(value)
    }
}

type LaterAlias = payload::Token;

fn main() {
    assert_eq!(LaterAlias::make(73).0, 73);
}
