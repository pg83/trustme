enum Leaf {
    Named { value: u8 },
}

impl Drop for Leaf {
    fn drop(&mut self) {}
}

enum Outer {
    Named { leaf: Leaf },
}

fn main() {
    let borrowed = &Outer::Named {
        leaf: Leaf::Named { value: 23 },
    };
    match borrowed {
        Outer::Named {
            leaf: Leaf::Named { value },
        } => assert_eq!(*value, 23),
    }
}
