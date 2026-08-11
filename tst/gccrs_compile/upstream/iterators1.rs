pub fn main() -> i32 {
    let mut iterator = (1..3).into_iter();
    while let Some(value) = iterator.next() {
        std::hint::black_box(value);
    }
    0
}
