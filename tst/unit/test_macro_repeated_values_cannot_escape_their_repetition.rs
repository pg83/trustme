//@ compile-fail: error:0: Variable $value is still repeating at this depth (0 < 1)

macro_rules! take_scalar {
    ($($value:expr),+) => {
        $value
    };
}

fn main() {
    let _ = take_scalar!(953u16, 967u16);
}
