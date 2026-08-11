//@ check-pass

fn main() {
    let strings = vec!["42", "tofu"];
    let mut errors = vec![];
    let numbers: Vec<_> = strings
        .into_iter()
        .map(|value| value.parse::<u8>())
        .filter_map(|result| result.map_err(|error| errors.push(error)).ok())
        .collect();
    println!("{numbers:?} {errors:?}");
}
