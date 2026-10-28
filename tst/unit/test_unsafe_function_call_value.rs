unsafe fn answer() -> u32 { 42 }
fn main() {
    assert_eq!(unsafe { answer() }, 42);
}
