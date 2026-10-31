const EDGE: u16 = 9;
fn main() {
    let closed = |value| match value { 2u16..=7 => true, _ => false };
    assert!(closed(5)); assert!(!closed(8));
    let open = |value| match value { ..EDGE => 1u8, EDGE.. => 2 };
    assert_eq!((open(8), open(9)), (1, 2));
    let text = |value| match value { "left" => 3u8, _ => 4 };
    assert_eq!((text("left"), text("right")), (3, 4));
    let bytes = |value: &[u8; 2]| match value { b"ab" => 5u8, _ => 6 };
    assert_eq!((bytes(b"ab"), bytes(b"cd")), (5, 6));
}
