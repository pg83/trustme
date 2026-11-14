#[path = "/"]
mod root {
    pub const VALUE: u32 = 42;
}

fn main() {
    assert_eq!(root::VALUE, 42);
}
