use std::mem::size_of;

macro_rules! same {
    ($left:expr, $right:expr) => {
        $left == $right
    };
}

fn main() {
    assert!(same!(size_of::<[u8; 1 << 4]>(), 16));
}
