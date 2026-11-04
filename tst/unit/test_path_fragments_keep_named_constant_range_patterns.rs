//@ compile-variant: -O
mod bounds {
    pub const LOW: u16 = 3;
    pub const HIGH: u16 = 7;
    pub const MARK: u16 = 11;
}

macro_rules! classify {
    ($lower:path, $upper:path, $mark:path) => {
        fn classify(value: u16) -> u8 {
            match value { $lower..=$upper => 13, $mark => 17, _ => 19 }
        }
    };
}
classify!(bounds::LOW, bounds::HIGH, bounds::MARK);

fn main() {
    for value in 0..=12 {
        let expected = match value { 3..=7 => 13, 11 => 17, _ => 19 };
        assert_eq!(classify(value), expected);
    }
}
