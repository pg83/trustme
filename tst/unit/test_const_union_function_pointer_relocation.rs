union Callback { call: fn(u16) -> u16, address: usize }
fn add_seven(value: u16) -> u16 { value + 7 }
const VALUE: Callback = Callback { call: add_seven };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe { assert_eq!((value.call)(35), 42); }
}
