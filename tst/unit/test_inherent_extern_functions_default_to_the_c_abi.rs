struct Abi;

impl Abi {
    extern fn value() -> u16 { 479 }
}

fn main() {
    let call: extern "C" fn() -> u16 = Abi::value;
    assert_eq!(call(), 479);
}
