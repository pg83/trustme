//@ edition: 2015

fn root_value() -> u32 {
    1711
}

macro_rules! local_root_value {
    () => { $crate::root_value() };
}

fn main() {
    assert_eq!(local_root_value!(), 1711);
}
