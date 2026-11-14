trait Same<Rhs: ?Sized = Self> {
    fn same(&self, rhs: &Rhs) -> bool;
}

impl Same for u8 {
    fn same(&self, rhs: &Self) -> bool {
        *self == *rhs
    }
}

fn checks_self(value: impl Same) -> bool {
    value.same(&value)
}

fn main() {
    assert!(checks_self(47_u8));
}
