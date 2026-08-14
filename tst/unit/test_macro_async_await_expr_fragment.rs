//@ edition: 2024

macro_rules! expression {
    ($value:expr) => {
        $value
    };
}

fn main() {
    let _future = async {
        let value = expression!(async { 1u8 }.await);
        assert_eq!(value, 1);
    };
}
