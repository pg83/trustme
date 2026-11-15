struct Inner(u8);
struct Outer(u8);

impl Drop for Outer {
    fn drop(&mut self) {
        let make: fn(u8) -> Inner = Inner;
        std::hint::black_box(make);
    }
}

fn main() {
    let make: fn(u8) -> Outer = Outer;
    std::hint::black_box(make);
}
