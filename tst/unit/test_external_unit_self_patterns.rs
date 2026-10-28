//@ aux-build: pattern_value_symbols.rs

trait Measure { fn measure(self) -> u8; }

impl Measure for pattern_value_symbols::Marker {
    fn measure(self) -> u8 {
        match self { Self => 7 }
    }
}

trait Check { fn check(self) -> u8; }

impl Check for pattern_value_symbols::Renamed {
    fn check(self) -> u8 {
        let Self = self;
        11
    }
}

fn main() {
    use pattern_value_symbols::{LIMIT, OTHER_LIMIT};
    assert_eq!(pattern_value_symbols::Marker.measure(), 7);
    assert_eq!(pattern_value_symbols::Renamed.check(), 11);
    assert!(matches!(std::hint::black_box(17u8), LIMIT));
    assert!(matches!(std::hint::black_box(17u8), OTHER_LIMIT));
}
