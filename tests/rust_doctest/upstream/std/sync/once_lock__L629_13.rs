// Extracted from library/std/src/sync/once_lock.rs:629
use std::sync::OnceLock;

fn main() -> Result<(), i32> {
let a = OnceLock::from(3);
let b = OnceLock::new();
b.set(3)?;
assert_eq!(a, b);
Ok(())
}
