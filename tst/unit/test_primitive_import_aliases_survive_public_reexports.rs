mod first {
    pub use core::primitive::{bool as Flag, u16 as Word};
}

mod second {
    pub use super::first::{Flag as Enabled, Word as Count};
    pub fn choose(enabled: Enabled, left: Count, right: Count) -> Count {
        if enabled { left } else { right }
    }
}

use second::{Count, Enabled};

fn main() {
    let enabled: Enabled = true;
    let value: Count = 317;
    assert_eq!(second::choose(enabled, value, 521), 317);
    assert_eq!(second::choose(false, value, 521), 521);
    assert_eq!(Count::MAX, u16::MAX);
}
