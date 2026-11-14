#[inline(never)]
fn roundtrip(value: &mut u32) {
    unsafe {
        llvm_asm!("" : "+r"(*value) : : : "volatile");
    }
}

fn main() {
    let mut value = 41u32;
    roundtrip(&mut value);
    assert_eq!(value, 41u32);
}
