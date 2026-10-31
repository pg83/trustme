struct Point { x: u8, y: u8 }
enum Choice { First(u8), Second(u8), Empty }
macro_rules! accepts {
    ($p:pat, $value:expr) => { matches!($value, $p) };
}
macro_rules! origin { () => { Point { x: 0, y: 0 } }; }
fn main() {
    assert!(accepts!(Point { x: 1, .. } | Point { y: 3, .. }, Point { x: 8, y: 3 }));
    assert!(!accepts!(Point { x: 1, .. } | Point { y: 3, .. }, Point { x: 8, y: 4 }));
    assert!(accepts!(Choice::First(2) | Choice::Second(2), Choice::Second(2)));
    assert!(!accepts!(Choice::First(2) | Choice::Second(2), Choice::Empty));
    assert!(accepts!((0, _) | (_, 4), (2, 4)));
    assert!(accepts!([0, ..] | [.., 9], [4, 9]));
    assert!(accepts!(origin!() | Point { x: 2, y: 2 }, Point { x: 0, y: 0 }));
    assert!(accepts!(Choice::First(1) | Choice::Second(1), Choice::First(1)));
}
