#![feature(associated_type_defaults)]

macro_rules! install_items {
    ($($item:item)*) => {
        trait Reader {
            $($item)*
        }
    };
}

install_items! {
    type Output = u16;
    const BASE: u16 = 1261;
    fn read(&self) -> u16 {
        1267
    }
}

struct Value;
impl Reader for Value {}

fn main() {
    let result: <Value as Reader>::Output = Value.read();
    assert_eq!(result + <Value as Reader>::BASE, 2528);
}
