//@ edition: 2018

macro_rules! two_patterns {
    ($left:pat | $right:pat) => {
        match Some(1u8) {
            $left | $right => true,
            _ => false,
        }
    };
}

fn main() {
    assert!(two_patterns!(Some(1u8) | None));
}
