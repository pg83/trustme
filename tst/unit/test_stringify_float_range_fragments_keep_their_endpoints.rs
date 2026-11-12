macro_rules! spelling {
    ($value:expr) => { stringify!($value) };
}

fn compact(text: &str) -> String {
    text.chars().filter(|c| !c.is_whitespace()).collect()
}

fn main() {
    assert_eq!(compact(spelling!(1.25..2.5)), "1.25..2.5");
    assert_eq!(compact(spelling!(-0.5..=3.75)), "-0.5..=3.75");
    assert_eq!(compact(spelling!(1.0..)), "1.0..");
    assert_eq!((1.25..2.5).start, 1.25);
    assert_eq!((-0.5..=3.75).end(), &3.75);
}
