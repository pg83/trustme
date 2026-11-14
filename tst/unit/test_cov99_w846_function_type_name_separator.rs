//@ edition: 2021

fn main() {
    assert_eq!(
        std::any::type_name::<fn(u8, u16) -> u32>(),
        "fn(u8, u16) -> u32"
    );
}
