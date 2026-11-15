pub trait Value {
    fn value(input: u32) -> u32;
}

impl Value for u32 {
    fn value(input: u32) -> u32 {
        input + 5
    }
}
