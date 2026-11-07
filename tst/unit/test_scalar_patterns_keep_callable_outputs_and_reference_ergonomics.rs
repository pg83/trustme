fn choose<F: Fn() -> bool>(callback: F) -> u16 {
    match callback() {
        true => 733,
        false => 739,
    }
}

fn number<F: Fn() -> u16>(callback: F) -> u16 {
    match callback() {
        0u16 => 743,
        1..=4 => 751,
        5u16.. => 757,
    }
}

fn main() {
    assert_eq!(choose(|| true), 733);
    assert_eq!(choose(|| false), 739);
    assert_eq!(number(|| 0), 743);
    assert_eq!(number(|| 3), 751);
    assert_eq!(number(|| 9), 757);
    assert!(matches!(&&3u16, 1u16..=4u16));
    assert!(matches!(&2.5f32, 2.5f32));
    assert!(matches!(&'q', 'p'..='r'));
    assert!(matches!(&7u8, ..=9u8));
    assert!(matches!(&11i16, 10i16..));
    assert!(matches!(&-1i8, -1i8));
    assert!(matches!(&b"ok", b"ok"));
}
