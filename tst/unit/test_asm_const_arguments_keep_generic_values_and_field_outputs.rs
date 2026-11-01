//@ compile-variant: -O

struct Result { value: u64, marker: u16 }
#[inline(never)]
fn add<const N: u64>(input: u64) -> Result {
    let mut result = Result { value: input, marker: 7 };
    unsafe {
        std::arch::asm!(
            "add {value}, {step}",
            value = inout(reg) result.value,
            step = const N,
            options(nomem, nostack),
        );
    }
    result
}
fn main() {
    let first = add::<11>(3);
    let second = add::<17>(5);
    assert_eq!((first.value, first.marker), (14, 7));
    assert_eq!((second.value, second.marker), (22, 7));
}
