fn main() {
    let matched = match i16::MIN {
        i16::MIN.. => true,
        _ => false,
    };
    assert!(matched);
}
