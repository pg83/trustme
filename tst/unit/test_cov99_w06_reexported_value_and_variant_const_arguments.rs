mod definitions {
    pub const fn width<const N: usize>() -> usize {
        N
    }

    pub struct Token<const N: usize>;
    pub struct Bundle<const N: usize>(pub [u8; N]);
    pub enum Choice<const N: usize> {
        Empty,
        Full([u8; N]),
    }
}

mod facade {
    pub use super::definitions::{
        width as length, Bundle as Packet, Choice::Empty as Nothing, Token as Marker,
    };
}

use facade::{length as measure, Marker as Flag, Nothing as Missing, Packet as Frame};

fn main() {
    let width = measure::<{ 1 + 2 }>;
    assert_eq!(width(), 3);
    let _: Flag<{ 1 + 2 }> = Flag::<{ 1 + 2 }>;
    let constructor = Frame::<{ 1 + 2 }>;
    let frame = constructor([4, 5, 6]);
    assert_eq!(frame.0, [4, 5, 6]);
    let missing = Missing::<{ 1 + 2 }>;
    assert!(matches!(missing, definitions::Choice::Empty));
}
