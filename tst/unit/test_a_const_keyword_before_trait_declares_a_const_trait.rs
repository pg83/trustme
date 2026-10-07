#![feature(const_trait_impl)]

pub const trait Double: Copy {
    fn double(self) -> Self;
}

macro_rules! declare {
    ($item:item) => {
        $item
    };
}

declare! {
    pub const trait Triple: Copy {
        fn triple(self) -> Self;
    }
}

impl const Double for u32 {
    fn double(self) -> u32 {
        self * 2
    }
}

impl const Triple for u32 {
    fn triple(self) -> u32 {
        self * 3
    }
}

const fn quadruple<T: [const] Double>(x: T) -> T {
    x.double().double()
}

const fn sextuple<T: [const] Double + [const] Triple>(x: T) -> T {
    x.double().triple()
}

const SIXTEEN: u32 = quadruple(4u32);
const TWELVE: u32 = sextuple(2u32);

fn main() {
    assert_eq!(SIXTEEN, 16);
    assert_eq!(TWELVE, 12);
    assert_eq!(quadruple(5u32), 20);
}
