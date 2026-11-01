fn main() {
    let value = Box::new([29u16, 31, 37]);
    match value {
        whole @ Box { .. } => assert_eq!(whole[1], 31),
    }
}
