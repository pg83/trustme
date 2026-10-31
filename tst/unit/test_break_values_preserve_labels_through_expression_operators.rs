#[derive(Clone, Copy)]
struct Pair { first: u16 }
impl Pair { fn value(self) -> u16 { self.first } }
macro_rules! check {
    ($value:expr, $expected:expr) => { assert!((loop { break $value }) == $expected) };
}
macro_rules! check_operators {
    ($pair:expr, $array:expr, $number:expr, $function:expr) => {{
        assert!((loop { break $pair.first }) == 13);
        assert!((loop { break $array[1] }) == 5);
        assert!((loop { break $number as u32 }) == 7);
        assert!((loop { break $number + 5 }) == 12);
        assert!((loop { break $pair.value() }) == 13);
        assert!((loop { break $function() }) == 17);
        assert!((loop { break $number .. 11 }) == (7..11));
    }};
}
macro_rules! assign {
    ($place:expr) => {{
        assert!((loop { break $place.first = 37 }) == ());
        assert!((loop { break $place.first += 5 }) == ());
    }};
}
fn main() {
    check!('inner: loop { break 'inner 37_u16 }, 37);
    check!('inner: while false { break 'inner }, ());
    check!('inner: for _item in [3, 5] { break 'inner }, ());
    check!('inner: { 13_u16 }, 13);
    check!(..7, ..7);
    let mut pair = Pair { first: 0 };
    assign!('place: { &mut pair });
    assert_eq!(pair.first, 42);
    check_operators!('pair: { Pair { first: 13 } }, 'array: { [3, 5, 8] }, 'number: { 7_u16 }, 'function: { || 17_u16 });
}
