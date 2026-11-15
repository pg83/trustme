//@ crate-type: lib
//@ compile-fail: Couldn't find pattern constant

trait CheckSelfPattern {
    fn check(self);
}

impl CheckSelfPattern for core::cmp::Ordering {
    fn check(self) {
        let Self = self;
    }
}
