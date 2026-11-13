macro_rules! beats {
    ($(beat)* beat beat beat stop) => {
        887u16
    };
}

fn main() {
    assert_eq!(beats!(beat beat beat stop), 887);
    assert_eq!(beats!(beat beat beat beat beat beat stop), 887);
}
