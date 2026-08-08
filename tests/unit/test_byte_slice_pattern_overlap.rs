fn exact_byte_first(values: &[u8]) -> u8 {
    match values {
        b"true" => 1,
        &[_, _, _, _] => 2,
        _ => 0,
    }
}

fn exact_slice_first(values: &[u8]) -> u8 {
    match values {
        &[_, _, _, _] => 1,
        b"true" => 2,
        _ => 0,
    }
}

fn split_byte_first(values: &[u8]) -> u8 {
    match values {
        b"true" => 1,
        &[b't', ..] => 2,
        _ => 0,
    }
}

fn split_slice_first(values: &[u8]) -> u8 {
    match values {
        &[b't', ..] => 1,
        b"true" => 2,
        _ => 0,
    }
}

fn disjoint_by_length(values: &[u8]) -> u8 {
    match values {
        &[_, _, _, _, _, ..] => 1,
        b"true" => 2,
        _ => 0,
    }
}

fn main() {
    assert_eq!(exact_byte_first(b"true"), 1);
    assert_eq!(exact_byte_first(b"rust"), 2);
    assert_eq!(exact_slice_first(b"true"), 1);

    assert_eq!(split_byte_first(b"true"), 1);
    assert_eq!(split_byte_first(b"trap"), 2);
    assert_eq!(split_slice_first(b"true"), 1);

    assert_eq!(disjoint_by_length(b"true"), 2);
    assert_eq!(disjoint_by_length(b"longer"), 1);
}
