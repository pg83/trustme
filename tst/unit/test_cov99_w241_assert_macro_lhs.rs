macro_rules! expected {
    () => { 7u8 };
}

fn main() {
    assert!({ expected!() == 7 });
}
