fn main() {
    let value: Box<dyn Fn(u16) -> u16> = Box::new(|value| value + 7);
    match value {
        whole @ Box { .. } => assert_eq!(whole(47), 54),
    }
}
