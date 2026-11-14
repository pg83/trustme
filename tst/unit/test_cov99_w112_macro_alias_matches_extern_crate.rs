mod scope {
    macro_rules! std {
        () => { 113u16 };
    }

    use std as first;
    use first as second;

    pub fn value() -> u16 {
        second!()
    }
}

fn main() {
    assert_eq!(scope::value(), 113);
}
