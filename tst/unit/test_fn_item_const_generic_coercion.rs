fn function<const N: usize>() {}

fn main() {
    let _ = if true { function::<{ 0 + 0 }> } else { function::<1> };
}
