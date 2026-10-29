#[path = "support/module_file_routes/root.rs"]
mod route;
fn main() { assert_eq!(route::branch::total(), 30); }
