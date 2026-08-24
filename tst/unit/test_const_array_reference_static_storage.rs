const NUMBERS: [u32; 1] = [1];

#[inline(never)]
fn array(index: usize) -> &'static u32 {
    &NUMBERS[index]
}

fn main() {
    assert_eq!(array(0).to_string(), "1");
}
