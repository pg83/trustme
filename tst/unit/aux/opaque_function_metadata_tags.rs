pub fn repeated<T: Copy>(value: T) -> impl Iterator<Item = T> {
    [value, value].into_iter()
}

pub fn callback(value: u16) -> impl Fn() -> u16 {
    move || value + 1
}
