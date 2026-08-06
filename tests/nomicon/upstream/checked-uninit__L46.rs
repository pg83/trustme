// Extracted from src/checked-uninit.md:46
fn main() {
    let x: i32;
    if true {
        x = 1;
    }
    println!("{}", x);
}
