macro_rules! method {
    () => {
        fn read(&self) -> u16 {
            1259
        }
    };
}

macro_rules! install_item {
    ($item:item) => {
        trait Reader {
            $item
        }
    };
}

install_item!(method!(););

struct Value;
impl Reader for Value {}

fn main() {
    assert_eq!(Value.read(), 1259);
}
