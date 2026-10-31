fn main() {
    let mut total = 0;
    'rows: for row in [[1u8, 2, 3], [4, 5, 6]] {
        for value in row {
            if value == 2 { continue 'rows; }
            total += value;
            if value == 5 { break 'rows; }
        }
    }
    assert_eq!(total, 10);
}
