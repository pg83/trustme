union Bits { boolean: bool, byte: u8 }
const VALUE: Bits = Bits { byte: 0xa5 };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!(value.byte, 0xa5); }
}
