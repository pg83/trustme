pub extern crate std as library;
mod route {
    pub use library as root;
    pub use root::stringify as text;
}
fn main() {
    assert_eq!(route::text!(retained), "retained");
    assert_eq!(route::root::mem::size_of::<u32>(), 4);
}
