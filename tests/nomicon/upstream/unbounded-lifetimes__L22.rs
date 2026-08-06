// Extracted from src/unbounded-lifetimes.md:22
fn get_str<'a>(s: *const String) -> &'a str {
    unsafe { &*s }
}

fn main() {
    let soon_dropped = String::from("hello");
    let dangling = get_str(&soon_dropped);
    drop(soon_dropped);
    println!("Invalid str: {}", dangling); // Invalid str: gӚ_`
}
