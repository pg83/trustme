union Bits { letter: char, number: u32 }
const VALUE: Bits = Bits { number: 0xffff_ffff };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!(value.number, 0xffff_ffff); }
}
