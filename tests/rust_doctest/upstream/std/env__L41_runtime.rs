// Extracted from library/std/src/env.rs:41
use std::env;

fn main() -> std::io::Result<()> {
    let path = env::current_dir()?;
    println!("The current directory is {}", path.display());
    Ok(())
}
