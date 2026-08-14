macro_rules! selected {
    (1) => {};
}

mod inner {
    selected!(1);

    macro_rules! selected {
        (2) => {};
    }
    selected!(2);

    macro_rules! selected {
        (3) => {};
    }
    selected!(3);
}

fn main() {}
