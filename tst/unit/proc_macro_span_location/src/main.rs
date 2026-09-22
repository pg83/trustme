use span_location_macro::{call_site_location, first_token_location};

fn main() {
    let (line, column, file) = call_site_location!();
    assert_eq!((line, column), (4, 32));
    assert!(file.ends_with("main.rs"), "{file}");
    let located = first_token_location!(abcdef);
    assert_eq!(located, (7, 41, 7, 41, 7, 47));
    let (_, _, _, _, end_line, end_column) = first_token_location!(
        xy
    );
    assert_eq!((end_line, end_column), (10, 11));
}
