macro_rules! contains {
    ($range:expr, $needle:expr, $expected:expr) => {
        assert!($range.contains(&$needle) == $expected);
    };
}
fn main() {
    contains!(331u16..337, 333, true);
    contains!(331u16..337, 337, false);
    contains!(331u16..=337, 337, true);
    contains!(331u16.., 337, true);
    contains!(..337u16, 337, false);
    contains!(..=337u16, 337, true);
}
