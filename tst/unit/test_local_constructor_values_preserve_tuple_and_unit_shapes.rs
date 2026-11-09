mod origin {
    pub struct Unit;
    pub struct Tuple(pub u8);

    pub enum Choice {
        Unit,
        Tuple(u8),
        Named { value: u8 },
    }

    impl Choice {
        pub fn make() -> Self {
            let constructor = Self::Tuple;
            constructor(23)
        }
    }
}

use origin::{Tuple as Pair, Unit as Marker};
use origin::Choice::{Tuple as Case, Unit as Empty};

fn main() {
    let constructor = Pair;
    assert_eq!(constructor(19).0, 19);
    let _marker = Marker;
    assert!(matches!(Empty, origin::Choice::Unit));
    assert!(matches!(Case(17), origin::Choice::Tuple(17)));
    assert!(matches!(origin::Choice::make(), origin::Choice::Tuple(23)));
    assert!(matches!(origin::Choice::Named { value: 29 }, origin::Choice::Named { value: 29 }));
}
