union Bits { boolean: bool, byte: u8 }
const VALUE: Bits = Bits { byte: 0xb3 };
const fn read(value: Bits) -> u8 { unsafe { value.byte } }
const FOLDED: u8 = read(VALUE);

#[inline(never)]
fn runtime_read() -> u8 { unsafe { VALUE.byte } }

fn main() {
    assert_eq!(FOLDED, 0xb3);
    assert_eq!(runtime_read(), 0xb3);
    assert_eq!(read(std::hint::black_box(VALUE)), 0xb3);
}
