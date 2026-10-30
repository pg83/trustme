fn moved(input: Option<(u8, u8)>) -> u8 {
    match input {
        Some((value, 0)) | Some((0, value)) if { return value; } => 255,
        _ => 7,
    }
}
fn borrowed(input: Option<(u8, u8)>) -> u8 {
    match input {
        Some((ref value, 0)) | Some((0, ref value)) if { return *value; } => 255,
        _ => 7,
    }
}
fn mutably_borrowed(mut input: Option<(u8, u8)>) -> u8 {
    match input {
        Some((ref mut value, 0)) | Some((0, ref mut value)) if { return *value; } => 255,
        _ => 7,
    }
}
fn main() {
    for function in [moved, borrowed, mutably_borrowed] {
        assert_eq!(function(Some((13, 0))), 13);
        assert_eq!(function(Some((0, 29))), 29);
        assert_eq!(function(Some((3, 5))), 7);
        assert_eq!(function(None), 7);
    }
}
