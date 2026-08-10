// Extracted from src/destructors.md:208
#![allow(unused)]
fn main() {
    struct PrintOnDrop(&'static str);
    impl Drop for PrintOnDrop {
        fn drop(&mut self) {
            println!("drop({})", self.0);
        }
    }
    let (declared_first, declared_last) = (
        PrintOnDrop("Dropped last"),
        PrintOnDrop("Dropped first"),
    );
}
