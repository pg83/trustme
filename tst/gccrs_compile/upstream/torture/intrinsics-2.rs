pub fn repeated_size_of() -> usize {
    let first = std::mem::size_of::<f32>();
    let second = std::mem::size_of::<f64>();
    let third = std::mem::size_of::<f32>();
    first + second + third
}
