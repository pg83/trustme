fn first<'a, 'b>(left: &'a u8, _: &'b u8) -> &'a u8 {
    left
}

fn second<'a, 'b>(_: &'a u8, right: &'b u8) -> &'b u8 {
    right
}

fn clone_both<'env>(value: &'env u8) -> u8 {
    let left: for<'a> fn(&'env u8, &'a u8) -> &'env u8 = first;
    let right: for<'a> fn(&'a u8, &'env u8) -> &'env u8 = second;
    let left = left.clone();
    let right = right.clone();
    *left(value, value) + *right(value, value)
}

fn main() {
    assert_eq!(clone_both(&21), 42);
}
