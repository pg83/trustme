macro_rules! twice { ($body:expr) => { ($body, $body) }; }
fn main() {
    let pair = twice!({
        trait Read {
            type Value;
            const BASE: Self::Value;
            fn read(&self) -> Self::Value;
        }
        struct Item(u16);
        impl Read for Item {
            type Value = u16;
            const BASE: u16 = 229;
            fn read(&self) -> u16 { self.0 }
        }
        Item(233).read() + <Item as Read>::BASE
    });
    assert_eq!(pair, (462, 462));
}
