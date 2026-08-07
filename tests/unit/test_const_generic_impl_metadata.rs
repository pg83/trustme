struct Inner<const N: usize, T> {
    data: [T; N],
}

struct Outer(Inner<4, u8>);

impl Outer {
    fn first(&self) -> u8 {
        self.0.data[0]
    }
}

fn main() {
    let value = Outer(Inner { data: [7; 4] });
    assert!(value.first() == 7);
}
