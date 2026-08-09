struct Value(u8);

fn main() {
    let values = [] as [Value; 0];
    assert_eq!(values.len(), 0);
}
