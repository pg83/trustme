//@ check-pass

#![feature(supertrait_item_shadowing)]

trait Base {
    fn linear(&self) -> u8 {
        1
    }
}

impl<T> Base for T {}

trait Child: Base {
    fn linear(&self) -> u8 {
        2
    }
}

impl<T> Child for T {}

trait Left {
    fn diamond(&self) -> u8 {
        1
    }
}

impl<T> Left for T {}

trait Right {
    fn diamond(&self) -> u8 {
        2
    }
}

impl<T> Right for T {}

trait Join: Left + Right {
    fn diamond(&self) -> u8 {
        3
    }
}

impl<T> Join for T {}

trait Deep: Join {
    fn diamond(&self) -> u8 {
        4
    }
}

impl<T> Deep for T {}

fn main() {
    assert!(().linear() == 2u8);
    assert!(().diamond() == 4u8);
}
