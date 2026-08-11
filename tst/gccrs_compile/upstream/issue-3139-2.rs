#[derive(Clone)] struct Abound { a: u32, b: u32 }
#[derive(Clone)] struct Be<T: Clone> { a: T, b: Abound }
fn main() { let value=Be{a:1usize,b:Abound{a:0,b:1}}; let _clone=value.clone(); }
