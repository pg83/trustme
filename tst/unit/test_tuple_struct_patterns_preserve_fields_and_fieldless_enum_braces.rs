struct Pair<T>(T, u8, u16);
enum Flag { Off, On }
fn select(value: Flag) -> u8 {
    match value { Flag::Off {} => 17, Flag::On {} => 23 }
}
fn main() {
    let value = Pair(17u16, 23, 37);
    assert_eq!(match value { Pair(head, .., tail) => head + tail }, 54);
    let Pair(head, middle, tail) = Pair(3u8, 5, 7);
    assert_eq!((head, middle, tail), (3, 5, 7));
    assert_eq!(select(Flag::Off), 17);
    assert_eq!(select(Flag::On), 23);
}
