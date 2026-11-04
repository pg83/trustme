//@ compile-variant: -O

macro_rules! render {
    ($range:expr) => { stringify!($range) };
}

fn main() {
    for (rendered, expected) in [
        (render!(1..4), "1..4"),
        (render!(1..=4), "1..=4"),
        (render!(..4), "..4"),
        (render!(..=4), "..=4"),
        (render!(1..), "1.."),
        (render!(..), ".."),
    ] {
        let compact: String = rendered.chars().filter(|ch| !ch.is_whitespace()).collect();
        assert_eq!(compact, expected);
    }
    let values = [3, 5, 7, 11, 13];
    assert_eq!(&values[1..4], &[5, 7, 11]);
    assert_eq!(&values[1..=4], &[5, 7, 11, 13]);
    assert_eq!((1..4).sum::<u32>(), 6);
    assert_eq!((1..=4).sum::<u32>(), 10);
}
