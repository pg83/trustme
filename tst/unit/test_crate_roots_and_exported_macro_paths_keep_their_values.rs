mod local {
    pub struct Value(pub u16);
}

macro_rules! local_type {
    () => { $crate::local::Value };
}

fn main() {
    let local: local_type!() = crate::local::Value(347);
    let values = ::std::vec![local.0, 349u16];
    assert_eq!(values.as_slice(), &[347, 349]);
}
