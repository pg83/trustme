// Extracted from src/fn/diverging.md:19
fn some_fn() {
    ()
}

fn main() {
    let _a: () = some_fn();
    println!("This function returns and you can see this line.");
}
