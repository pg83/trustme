pub fn main() {
    if true {
        for _ in 20usize..40usize {
            std::hint::black_box("loop");
        }
    }
}
