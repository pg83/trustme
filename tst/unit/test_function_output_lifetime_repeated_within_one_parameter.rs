fn select<'a>(input: (&'a u32, &'a u32)) -> &u32 {
    input.0
}

fn main() {
    let value = 1;
    assert_eq!(*select((&value, &value)), 1);
}
