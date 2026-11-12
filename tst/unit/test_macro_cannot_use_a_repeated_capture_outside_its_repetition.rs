//@ compile-fail: repeating

macro_rules! scalar {
    ($($value:expr),+) => {
        $value
    };
}

fn main() {
    let _ = scalar!(173u16, 179u16);
}
